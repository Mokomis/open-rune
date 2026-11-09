
#include "Precomp.h"
#include "TextureManager.h"
#include "VulkanRenderDevice.h"
#include "CachedTexture.h"
#include "MaterialRelief.h"
#include "MaterialMaps.h"
#include "Package/Package.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Render/RenderSubsystem.h"
#include "Utils/CommandLine.h"
#include "RenderDevice/VisualComparison.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <surrealgpu/vulkanbuilders.h>

namespace
{
std::filesystem::path ReplacementRoot()
{
    auto overridePath=commandline->GetArg("", "--texture-pack-root", "");
    return overridePath.empty()?std::filesystem::path(engine->LaunchInfo.gameRootFolder)/"TexturePacks"/"OpenRune":std::filesystem::path(overridePath);
}
}

TextureManager::TextureManager(VulkanRenderDevice* renderer) : renderer(renderer)
{
	CreateNullTexture();
	CreateDitherTexture();
}

TextureManager::~TextureManager()
{
}

std::string TextureManager::TextureIdentity(TextureInfo* info) const
{
    if(!info || !info->Texture || !info->Texture->package) return {};
    auto* texture=info->Texture;auto* package=texture->package;
    std::string name;std::set<int> visited;
    for(int index=int(texture->exportIndex)+1;index>0;index=package->GetExportEntry(index)->ObjOuter)
    {
        if(index>package->GetExportCount() || !visited.insert(index).second) return {};
        name=package->GetName(package->GetExportEntry(index)->ObjName).ToString()+(name.empty()?"":"."+name);
    }
    return TexturePack::IdentityKey(package->GetPackageName().ToString()+"."+name);
}

std::string TextureManager::ReplacementKey(TextureInfo* info)
{
    if (!info || !info->Texture || !info->Texture->package ||
        info->Format!=TextureFormat::P8 || !info->Palette || info->NumMips<1 ||
        info->Texture->bRealtime() || info->Texture->AnimNext() || info->bRealtimeChanged) return {};
    auto existing=PackKeys.find(info->CacheID);
    if(existing!=PackKeys.end()) return existing->second;
    auto& result=PackKeys[info->CacheID]; // Negative lookups are cached too.
    try
    {
        auto root=ReplacementRoot();
        if(!PackIndexLoaded)
        {
            PackIndexLoaded=true;PackIndex=TexturePack::ReadIndex(root/"index.tsv");
            LogMessage("RUNE_TEXTURE_PACK_INDEX entries="+std::to_string(PackIndex.size()));
        }
        auto name=TextureIdentity(info);
        if(name.empty()) return {};
        auto entry=PackIndex.find(name);
        if(entry==PackIndex.end() || entry->second.width!=info->USize || entry->second.height!=info->VSize ||
            info->Mips[0].Data.size()<size_t(info->USize)*info->VSize) return {};
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<size_t(info->USize)*info->VSize;++i)
        {
            auto c=info->Palette[info->Mips[0].Data[i]];
            hash=TexturePack::HashByte(hash,c.R);hash=TexturePack::HashByte(hash,c.G);hash=TexturePack::HashByte(hash,c.B);
        }
        if(hash==entry->second.hash) result=name;
        else LogMessage("RUNE_TEXTURE_PACK_FALLBACK source_mismatch="+name);
    }
    catch(const std::exception& e) { LogMessage(std::string("RUNE_TEXTURE_PACK_FALLBACK ")+e.what()); }
    return result;
}

bool TextureManager::UploadReplacement(CachedTexture* target, TextureInfo* info, const char* channel)
{
    // Original artwork may use prepared 1x material data, but never a replacement
    // color/mask. Missing channels in older packs retain the procedural fallback.
    bool material=std::string(channel)=="normal" || std::string(channel)=="response" || std::string(channel)=="relief";
    if (!renderer->TexturePackQuality && !material) return false;
    auto name=ReplacementKey(info);if(name.empty()) return false;
    int quality=renderer->TexturePackQuality;
    if(material && quality)
    {
        // Partial packs/budget fallback can leave this asset's color original.
        // Match the color that was actually uploaded, not only the global menu.
        bool colorKnown=false,replaced=false;
        for(auto& cache:TextureCache)
        {
            auto color=cache.find(info->CacheID);
            if(color!=cache.end() && color->second) {colorKnown=true;replaced|=color->second->IsReplacement;}
        }
        if(colorKnown && !replaced) quality=0;
    }
    auto root=ReplacementRoot()/name/
        (quality==2?"4x":quality==1?"2x":"1x");
    for(bool compressed : {true,false})
    {
        bool uploading=false;
        try
        {
            auto path=root/(std::string(channel)+(compressed?".astc.ktx":".ktx"));
            if(!std::filesystem::exists(path)) continue;
            auto image=TexturePack::Read(path);
            if(image.format!=TexturePack::RGBA8 && !renderer->Device->EnabledFeatures.Features.textureCompressionASTC_LDR) continue;
            VkFormat format=image.format==TexturePack::ASTC6?VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
                image.format==TexturePack::ASTC4?VK_FORMAT_ASTC_4x4_UNORM_BLOCK:VK_FORMAT_R8G8B8A8_UNORM;
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(renderer->Device->PhysicalDevice.Device,format,&properties);
            auto required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            if((properties.optimalTilingFeatures&required)!=required) continue;
            int scale=quality==2?4:quality==1?2:1;
            while(std::max(info->USize,info->VSize)*scale>2048) scale/=2;
            if(image.levels[0].width!=info->USize*scale || image.levels[0].height!=info->VSize*scale ||
                uint32_t(image.levels[0].width)>renderer->Device->PhysicalDevice.Properties.Properties.limits.maxImageDimension2D ||
                uint32_t(image.levels[0].height)>renderer->Device->PhysicalDevice.Properties.Properties.limits.maxImageDimension2D)
                throw std::runtime_error("Replacement dimensions mismatch");
            // Conservative payload budget including a per-image allocation allowance.
            size_t charge=image.bytes+65536;
            if(charge>TexturePack::BudgetBytes-PackBytes) return false;
            Array<UnrealMipmap> mips;
            for(auto& level:image.levels)
            {
                UnrealMipmap mip;mip.Width=level.width;mip.Height=level.height;
                mip.Data.resize(level.bytes.size());std::copy(level.bytes.begin(),level.bytes.end(),mip.Data.begin());
                mips.push_back(std::move(mip));
            }
            TextureInfo upload;upload.USize=mips[0].Width;upload.VSize=mips[0].Height;upload.NumMips=int(mips.size());upload.Mips=mips.data();
            upload.Format=image.format==TexturePack::ASTC6?TextureFormat::ASTC_6x6:
                image.format==TexturePack::ASTC4?TextureFormat::ASTC_4x4:TextureFormat::RGBA8_;
            uploading=true;
            renderer->Uploads->UploadTexture(target,upload,false);
            target->IsReplacement=true;
            PackBytes+=charge;++PackLoaded;
            VisualComparison::TexturePackBytes.store(PackBytes);VisualComparison::TexturePackLoaded.store(PackLoaded);
            LogMessage("RUNE_TEXTURE_PACK_LOADED id="+name+" channel="+channel+" format="+(image.format==TexturePack::RGBA8?"rgba8":"astc")+
                " size="+std::to_string(upload.USize)+"x"+std::to_string(upload.VSize)+" mips="+std::to_string(upload.NumMips)+" budget_bytes="+std::to_string(PackBytes)+" tier="+std::to_string(scale));
            return true;
        }
        catch(const std::exception& e)
        {
            if(uploading) throw; // Never reuse a partially submitted GPU allocation.
            LogMessage("RUNE_TEXTURE_PACK_FALLBACK id="+name+" reason="+e.what());
        }
    }
    return false;
}

CachedTexture* TextureManager::GetMaterialResponseMap(TextureInfo* info)
{
    if(!info || info->WetGore) return nullptr;
    auto found=ResponseCache.find(info->CacheID);if(found!=ResponseCache.end())return found->second.get();
    auto texture=std::make_unique<CachedTexture>();
    if(!UploadReplacement(texture.get(),info,"response")) texture.reset();
    auto* result=texture.get();ResponseCache[info->CacheID]=std::move(texture);return result;
}

void TextureManager::ValidateReplacementUploads()
{
    // Opt-in fixture: real owned assets, normal upload/descriptors, no package edits.
    bool materialProbe=commandline->HasArg("", "--probe-material-pack");
    if(!materialProbe && renderer->TexturePackQuality!=2) throw std::runtime_error("Texture probe requires 4x pack");
    std::vector<std::pair<std::string,std::string>> samples={{"Players","Ragnarragv_head"}, {"weapons","handaxehandaxe"}, {"Brckston","Brck14_d"}, {"Village","VArchDoor"}};
    auto originalOnly=commandline->GetArg("", "--probe-original-material", "");
    if(materialProbe && !originalOnly.empty())
    {
        auto dot=originalOnly.find('.');
        if(dot==std::string::npos) throw std::runtime_error("Original material probe needs package.texture");
        samples.push_back({originalOnly.substr(0,dot),originalOnly.substr(dot+1)});
    }
    for(auto pair : samples)
    {
        auto* texture=UObject::Cast<UTexture>(engine->packages->GetPackage(pair.first)->GetUObject("Texture",pair.second));
        TextureInfo info;engine->render->UpdateTextureInfo(info,texture);
        auto original=info;auto data=info.Mips[0].Data;
        auto* color=GetTexture(&info,false);auto* masked=GetTexture(&info,true);
        auto* normal=GetMaterialMap(&info);auto* response=GetMaterialResponseMap(&info);
        std::vector<CachedTexture*> maps=materialProbe?std::vector<CachedTexture*>{normal,response,GetRelief(&info)}:
            std::vector<CachedTexture*>{color,masked,normal,response};
        for(auto* uploaded : maps)
        {
            if(!uploaded || !uploaded->IsReplacement) throw std::runtime_error("Missing sample replacement");
            renderer->DescriptorSets->GetTextureArrayIndex(0,uploaded);
        }
        if(materialProbe && !renderer->TexturePackQuality && (color->IsReplacement || masked->IsReplacement))
            throw std::runtime_error("Original artwork was replaced by material-only pack");
        if(materialProbe && pair.first+"."+pair.second==originalOnly && (color->IsReplacement || masked->IsReplacement))
            throw std::runtime_error("Original-only fixture must not contain remastered color");
        if(info.USize!=original.USize || info.VSize!=original.VSize || info.UScale!=original.UScale ||
            info.VScale!=original.VScale || info.Mips!=original.Mips || info.Mips[0].Data!=data)
            throw std::runtime_error("Texture probe: source or UV metadata changed");
    }
    LogMessage(materialProbe?"RUNE_MATERIAL_PACK_PROBE_OK assets="+std::to_string(samples.size())+" channels=3 original_uv_and_pixels=preserved":
        "RUNE_TEXTURE_PACK_PROBE_OK assets=4 channels=4 original_uv_and_pixels=preserved");
}

void TextureManager::UpdateTextureRect(TextureInfo* info, int x, int y, int w, int h)
{
	std::unique_ptr<CachedTexture>& tex = TextureCache[0][info->CacheID];
	if (tex)
	{
		renderer->Uploads->UploadTextureRect(tex.get(), *info, x, y, w, h);
		info->bRealtimeChanged = 0;
	}
}

CachedTexture* TextureManager::GetTexture(TextureInfo* info, bool masked)
{
	if (!info)
		return nullptr;

    if(commandline->HasArg("", "--probe-texture-usage"))
    {
        auto name=TextureIdentity(info);
        if(!name.empty() && UsageKeys.insert(name).second)
            LogMessage("RUNE_TEXTURE_USAGE id="+name);
    }

	if (info->Texture && (info->Texture->PolyFlags() & PF_Masked))
		masked = true;

	if (info->Format != TextureFormat::P8)
		masked = false;

	std::unique_ptr<CachedTexture>& tex = TextureCache[(int)masked][info->CacheID];
	if (!tex)
	{
		tex.reset(new CachedTexture());
        if (!UploadReplacement(tex.get(), info, masked ? "masked" : "color"))
            renderer->Uploads->UploadTexture(tex.get(), *info, masked);
	}
	else if (info->bRealtimeChanged)
	{
        if(tex->IsReplacement)
        {
            renderer->Flush(false);
            return GetTexture(info,masked);
        }
		info->bRealtimeChanged = 0;
		renderer->Uploads->UploadTexture(tex.get(), *info, masked);
	}
	return tex.get();
}

CachedTexture* TextureManager::GetRelief(TextureInfo* info)
{
    if (!info || !info->Texture || !info->Texture->package || info->Format != TextureFormat::P8 ||
        !info->Palette || info->NumMips < 1 || info->USize < 4 || info->VSize < 4 ||
        info->USize > 1024 || info->VSize > 1024 || info->Texture->bRealtime() || info->Texture->AnimNext()) return nullptr;
    auto found=ReliefCache.find(info->CacheID);
    if(found!=ReliefCache.end()) return found->second.get();
    auto authored=std::make_unique<CachedTexture>();
    if(UploadReplacement(authored.get(),info,"relief"))
    {
        auto* result=authored.get();ReliefCache[info->CacheID]=std::move(authored);return result;
    }
    auto package = info->Texture->package->GetPackageName();
    if (package != "Wood" && package != "Rock" && package != "Brckston") return nullptr;
    auto& cached = ReliefCache[info->CacheID];
    if (cached) return cached.get();
    if (info->Mips[0].Data.size() < (size_t)info->USize * info->VSize) return nullptr;
    std::vector<float> heights((size_t)info->USize*info->VSize);
    for(size_t i=0;i<heights.size();i++)
    {
        const auto& c = info->Palette[info->Mips[0].Data[i]];
        heights[i]=(0.2126f*c.R+0.7152f*c.G+0.0722f*c.B)/255.0f;
    }
    Array<UnrealMipmap> mips;
    int w=info->USize,h=info->VSize;
    while(true)
    {
        UnrealMipmap mip; mip.Width=w;mip.Height=h;
        auto pixels=MaterialRelief::Pack(heights,w,h,1.3f);
        mip.Data.resize(pixels.size());std::copy(pixels.begin(),pixels.end(),mip.Data.begin());
        mips.push_back(std::move(mip));
        if(w==1 && h==1) break;
        int nw=std::max(w/2,1),nh=std::max(h/2,1);
        std::vector<float> small((size_t)nw*nh);
        for(int y=0;y<nh;y++) for(int x=0;x<nw;x++)
            small[y*nw+x]=0.25f*(heights[(2*y%h)*w+2*x%w]+heights[(2*y%h)*w+(2*x+1)%w]+
                heights[((2*y+1)%h)*w+2*x%w]+heights[((2*y+1)%h)*w+(2*x+1)%w]);
        heights=std::move(small);w=nw;h=nh;
    }
    TextureInfo generated;generated.Format=TextureFormat::RGBA8_;generated.USize=info->USize;generated.VSize=info->VSize;
    generated.NumMips=(int)mips.size();generated.Mips=mips.data();
    cached=std::make_unique<CachedTexture>();
    renderer->Uploads->UploadTexture(cached.get(),generated,false);
    LogMessage("RUNE_MATERIAL_GENERATED package="+package.ToString()+" texture="+info->Texture->Name.ToString()+" size="+std::to_string(info->USize)+"x"+std::to_string(info->VSize));
    return cached.get();
}

void TextureManager::ClearCache()
{
    ResponseCache.clear(); PackKeys.clear(); PackIndex.clear(); PackIndexLoaded=false;
    PackBytes=0; PackLoaded=0;
    VisualComparison::TexturePackBytes.store(0); VisualComparison::TexturePackLoaded.store(0);
	ReliefCache.clear();
    MaterialCache.clear();
	for (auto& cache : TextureCache)
	{
		cache.clear();
	}
}

void TextureManager::ClearAllBindlessIndexes()
{
    for (auto& it : ResponseCache) if(it.second) for (int& index : it.second->BindlessIndex) index = -1;
	for (auto& it : ReliefCache) for (int& index : it.second->BindlessIndex) index = -1;
    for (auto& it : MaterialCache) for (int& index : it.second->BindlessIndex) index = -1;
	for (auto& cache : TextureCache)
	{
		for (auto& it : cache)
		{
			for (int& index : it.second->BindlessIndex)
				index = -1;
		}
	}
}

void TextureManager::CreateNullTexture()
{
	auto cmdbuffer = renderer->Commands->GetTransferCommands();

	NullTexture = ImageBuilder()
		.Format(VK_FORMAT_R8G8B8A8_UNORM)
		.Size(1, 1)
		.Usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("NullTexture")
		.Create(renderer->Device.get());

	NullTextureView = ImageViewBuilder()
		.Image(NullTexture.get(), VK_FORMAT_R8G8B8A8_UNORM)
		.DebugName("NullTextureView")
		.Create(renderer->Device.get());

	auto stagingbuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.Size(4)
		.DebugName("NullTextureStaging")
		.Create(renderer->Device.get());

	auto data = (uint32_t*)stagingbuffer->Map(0, 4);
	data[0] = 0xffffffff;
	stagingbuffer->Unmap();

	PipelineBarrier()
		.AddImage(NullTexture.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { 1, 1, 1 };
	cmdbuffer->copyBufferToImage(stagingbuffer->buffer, NullTexture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	PipelineBarrier()
		.AddImage(NullTexture.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	renderer->Commands->FrameDeleteList->buffers.push_back(std::move(stagingbuffer));
}

void TextureManager::CreateDitherTexture()
{
	static const float ditherdata[64] =
	{
			.0078125, .2578125, .1328125, .3828125, .0234375, .2734375, .1484375, .3984375,
			.7578125, .5078125, .8828125, .6328125, .7734375, .5234375, .8984375, .6484375,
			.0703125, .3203125, .1953125, .4453125, .0859375, .3359375, .2109375, .4609375,
			.8203125, .5703125, .9453125, .6953125, .8359375, .5859375, .9609375, .7109375,
			.0390625, .2890625, .1640625, .4140625, .0546875, .3046875, .1796875, .4296875,
			.7890625, .5390625, .9140625, .6640625, .8046875, .5546875, .9296875, .6796875,
			.1015625, .3515625, .2265625, .4765625, .1171875, .3671875, .2421875, .4921875,
			.8515625, .6015625, .9765625, .7265625, .8671875, .6171875, .9921875, .7421875,
	};

	auto cmdbuffer = renderer->Commands->GetTransferCommands();

	DitherImage = ImageBuilder()
		.Format(VK_FORMAT_R32_SFLOAT)
		.Size(8, 8)
		.Usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("DitherImage")
		.Create(renderer->Device.get());

	DitherImageView = ImageViewBuilder()
		.Image(DitherImage.get(), VK_FORMAT_R32_SFLOAT)
		.DebugName("DitherImageView")
		.Create(renderer->Device.get());

	auto stagingbuffer = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.Size(sizeof(ditherdata))
		.DebugName("DitherImageStaging")
		.Create(renderer->Device.get());

	auto data = (uint32_t*)stagingbuffer->Map(0, sizeof(ditherdata));
	memcpy(data, ditherdata, sizeof(ditherdata));
	stagingbuffer->Unmap();

	PipelineBarrier()
		.AddImage(DitherImage.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { 8, 8, 1 };
	cmdbuffer->copyBufferToImage(stagingbuffer->buffer, DitherImage->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	PipelineBarrier()
		.AddImage(DitherImage.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
		.Execute(cmdbuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	renderer->Commands->FrameDeleteList->buffers.push_back(std::move(stagingbuffer));
}

CachedTexture* TextureManager::GetMaterialMap(TextureInfo* info)
{
    if(info && info->WetGore)return nullptr; // Wet cut surfaces keep their dedicated roughness.
    if(!info || !info->Texture || !info->Texture->package || info->Format!=TextureFormat::P8 || !info->Palette ||
        info->NumMips<1 || info->USize<4 || info->VSize<4 || info->USize>1024 || info->VSize>1024 ||
        info->Texture->bRealtime() || info->Texture->AnimNext()) return nullptr;
    auto found=MaterialCache.find(info->CacheID);if(found!=MaterialCache.end())return found->second.get();
    auto authored=std::make_unique<CachedTexture>();
    if (UploadReplacement(authored.get(),info,"normal"))
    {
        auto* result=authored.get();MaterialCache[info->CacheID]=std::move(authored);return result;
    }
    int w=info->USize,h=info->VSize;
    if(info->Mips[0].Data.size()<(size_t)w*h)return nullptr;
    std::vector<float> luma((size_t)w*h),warmth(luma.size());
    for(size_t i=0;i<luma.size();++i) {
        const auto& c=info->Palette[info->Mips[0].Data[i]];
        luma[i]=(c.R*0.2126f+c.G*0.7152f+c.B*0.0722f)/255;
        warmth[i]=(float(c.R)-std::min(c.G,c.B))/255;
    }
    auto name=info->Texture->package->GetPackageName().ToString()+"."+info->Texture->Name.ToString();
    auto kind=MaterialMaps::Classify(name);
    auto pixels=MaterialMaps::Generate(luma,warmth,w,h,kind);
    Array<UnrealMipmap> mips;
    while(true) {
        UnrealMipmap mip;mip.Width=w;mip.Height=h;mip.Data.resize(pixels.size());std::copy(pixels.begin(),pixels.end(),mip.Data.begin());mips.push_back(std::move(mip));
        if(w==1 && h==1)break;
        pixels=MaterialMaps::Downsample(pixels,w,h);w=std::max(1,w/2);h=std::max(1,h/2);
    }
    TextureInfo generated;generated.Format=TextureFormat::RGBA8_;generated.USize=info->USize;generated.VSize=info->VSize;
    generated.NumMips=int(mips.size());generated.Mips=mips.data();
    auto texture=std::make_unique<CachedTexture>();renderer->Uploads->UploadTexture(texture.get(),generated,false);
    auto* result=texture.get();MaterialCache[info->CacheID]=std::move(texture);
    LogMessage("RUNE_MATERIAL_MAP texture="+name+" profile="+std::to_string(kind)+" size="+std::to_string(info->USize)+"x"+std::to_string(info->VSize));
    return result;
}
