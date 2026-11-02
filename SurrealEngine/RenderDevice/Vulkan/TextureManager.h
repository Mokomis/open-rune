#pragma once

#include "SceneTextures.h"
#include "RenderDevice/TexturePack.h"

struct TextureInfo;
class VulkanRenderDevice;
class CachedTexture;

class TextureManager
{
public:
	TextureManager(VulkanRenderDevice* renderer);
	~TextureManager();

	void UpdateTextureRect(TextureInfo* info, int x, int y, int w, int h);
	CachedTexture* GetTexture(TextureInfo* info, bool masked);
	CachedTexture* GetRelief(TextureInfo* info);
    CachedTexture* GetMaterialMap(TextureInfo* info);
    CachedTexture* GetMaterialResponseMap(TextureInfo* info);
    void ValidateReplacementUploads();

	void ClearCache();
	void ClearAllBindlessIndexes();

	std::unique_ptr<VulkanImage> NullTexture;
	std::unique_ptr<VulkanImageView> NullTextureView;

	std::unique_ptr<VulkanImage> DitherImage;
	std::unique_ptr<VulkanImageView> DitherImageView;

	std::unique_ptr<SceneTextures> Scene;

	int GetTexturesInCache() { return (int)(TextureCache[0].size() + TextureCache[1].size()); }

private:
	void CreateNullTexture();
	void CreateDitherTexture();
    bool UploadReplacement(CachedTexture* target, TextureInfo* info, const char* channel);
    std::string TextureIdentity(TextureInfo* info) const;
    std::string ReplacementKey(TextureInfo* info);
    bool PackIndexLoaded = false;
    std::map<std::string, TexturePack::Entry> PackIndex;
    std::unordered_map<uint64_t, std::string> PackKeys;
    std::set<std::string> UsageKeys;
    size_t PackBytes = 0;
    int PackLoaded = 0;
    std::unordered_map<uint64_t, std::unique_ptr<CachedTexture>> ResponseCache;

	VulkanRenderDevice* renderer = nullptr;
	std::unordered_map<uint64_t, std::unique_ptr<CachedTexture>> TextureCache[2];
	std::unordered_map<uint64_t, std::unique_ptr<CachedTexture>> ReliefCache;
    std::unordered_map<uint64_t, std::unique_ptr<CachedTexture>> MaterialCache;
};
