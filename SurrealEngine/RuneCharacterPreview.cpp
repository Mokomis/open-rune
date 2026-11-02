#include "Precomp.h"
#include "RuneCharacterPreview.h"
#include "RuneCharacters.h"
#include "RuneMenu.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "RenderDevice/TexturePack.h"
#include <map>
#include <set>
#include <chrono>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace RuneCharacterPreview {
namespace {
using Clock=std::chrono::steady_clock;
struct Card { Clock::time_point requested{};std::vector<uint32_t> pixels;bool failed=false;float yaw=std::numeric_limits<float>::quiet_NaN();uint64_t generation=0,delivered=0; };
std::array<Card,74> cards;
std::mutex mutex;
Clock::time_point lastTick{};
// Offline atlas review only. Runtime cards continue to use their normal source
// textures; this override is enabled exclusively inside the isolated probe.
struct ReviewTexture { int width=0,height=0;std::vector<uint8_t> rgb; };
std::filesystem::path reviewRoot;
std::map<UTexture*,ReviewTexture> reviewTextures;
const ReviewTexture* ReviewAtlas(UTexture* texture) {
    if(reviewRoot.empty())return nullptr;
    auto [it,inserted]=reviewTextures.try_emplace(texture);
    if(inserted) {
        auto* package=texture->package;std::string name;std::set<int> seen;
        for(int index=int(texture->exportIndex)+1;index>0;index=package->GetExportEntry(index)->ObjOuter) {
            if(index>package->GetExportCount() || !seen.insert(index).second)throw std::runtime_error("Invalid review texture identity");
            name=package->GetName(package->GetExportEntry(index)->ObjName).ToString()+(name.empty()?"":"."+name);
        }
        name=TexturePack::Key(package->GetPackageName().ToString()+"."+name);
        auto path=reviewRoot/(name+"-color.ppm");
        if(std::filesystem::exists(path)) {
            std::ifstream file(path,std::ios::binary);std::string magic;int maximum=0;auto& t=it->second;
            file>>magic>>t.width>>t.height>>maximum;
            if(!file || magic!="P6" || maximum!=255 || t.width<1 || t.height<1 || t.width>4096 || t.height>4096 || file.get()!='\n')throw std::runtime_error("Invalid review PPM");
            t.rgb.resize(size_t(t.width)*t.height*3);file.read(reinterpret_cast<char*>(t.rgb.data()),t.rgb.size());
            if(!file)throw std::runtime_error("Truncated review PPM");
            std::cout<<"CHARACTER_REVIEW_ATLAS "<<name<<std::endl;
        }
    }
    return it->second.rgb.empty()?nullptr:&it->second;
}
int key(int character,int skin) {return skin==1?36:character;}
openrune::CharacterChoice choice(int index){return index==36?openrune::CharacterChoice{11,1}:openrune::CharacterChoice{uint8_t(index),0};}
bool valid(int character,int skin) {return character>=0 && character<36 && skin>=0 && skin<openrune::Characters[character].skins;}

std::vector<uint32_t> Render(Engine& game,openrune::CharacterChoice selected,float seconds,int Width=RuneCharacterPreview::Width,int Height=RuneCharacterPreview::Height,float yaw=std::numeric_limits<float>::quiet_NaN()) {
    // Read class defaults only: never spawn a preview pawn in a live world or
    // run scripts on it. Model poses and texture samples are private temporaries.
    auto* actor=game.packages->FindClass(openrune::Characters[selected.character].className)->GetDefaultObject<UActor>();
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));auto* geometry=GetRuneGeometry(actor);
    if(!model || !geometry || geometry->Meshes.empty())throw std::runtime_error("Preview body unavailable");
    size_t variant=actor->GetByte("SkelMesh");if(variant>=geometry->Meshes.size())variant=0;
    const auto& mesh=geometry->Meshes[variant];
    size_t seq=0;bool idle=false;
    for(size_t i=0;i<model->Sequences.size();++i)if(model->Sequences[i].Info.Name=="neutral_idle"){seq=i;idle=true;break;}
    if(!idle)throw std::runtime_error("Preview idle unavailable");
    const auto& anim=model->Sequences[seq].Info;
    auto bones=model->JointMatrices(model->SampleFrame(seq,std::fmod(seconds*1.25f*anim.Rate/anim.NumFrames,1.f),true));
    auto rest=model->JointMatrices(model->SampleFrame(seq,0,true));
    auto skin=[](const RuneVertex& v,const Array<mat4>& matrices){
        if(v.Joint[0]<0 || size_t(v.Joint[0])>=matrices.size() || (v.Weight!=1 && (v.Joint[1]<0 || size_t(v.Joint[1])>=matrices.size())))throw std::runtime_error("Preview joint invalid");
        auto p=(matrices[v.Joint[0]]*vec4(v.Point[0],1)).xyz()*v.Weight;
        if(v.Weight!=1)p+=(matrices[v.Joint[1]]*vec4(v.Point[1],1)).xyz()*(1-v.Weight);return p;
    };
    vec3 low(1e9f),high(-1e9f);
    for(const auto& v:mesh.Vertices){auto p=skin(v,rest);for(int i=0;i<3;++i){low[i]=std::min(low[i],p[i]);high[i]=std::max(high[i],p[i]);}}
    float scale=std::min((Height-18.f)/std::max(1.f,high.y-low.y),(Width-24.f)/std::max(1.f,high.x-low.x));
    float angle=3.14159265f+.22f+(std::isfinite(yaw)?yaw:.18f*std::sin(seconds*.65f)),cs=std::cos(angle),sn=std::sin(angle);
    std::vector<vec3> vertices;vertices.reserve(mesh.Vertices.size());
    for(const auto& v:mesh.Vertices){auto p=skin(v,bones);float x=p.x-(low.x+high.x)*.5f,z=p.z-(low.z+high.z)*.5f;
        vertices.emplace_back(Width*.5f+(x*cs+z*sn)*scale,Height-9-(p.y-low.y)*scale,-(-x*sn+z*cs));}
    std::array<UTexture*,16> textures{};
    auto** defaults=static_cast<UObject**>(actor->GetProperty("SkelGroupSkins"));
    for(int i=0;i<16;++i){textures[i]=UObject::TryCast<UTexture>(defaults[i]);
        if(!textures[i] && !mesh.SkinNames[i].IsNone())textures[i]=UObject::TryCast<UTexture>(geometry->package->GetUObject("Texture",mesh.SkinNames[i]));
        if(selected.skin==1 && i>=1 && i<=13)textures[i]=UObject::TryCast<UTexture>(game.packages->GetPackage("Players")->GetUObject("Texture",i==1?"ragnarJarl_chest":i==5?"ragnarJarl_head":"ragnarJarl_armleg"));
    }
    std::vector<uint32_t> pixels(Width*Height);std::vector<float> depth(Width*Height,1e9f);
    auto edge=[](const vec3& a,const vec3& b,float x,float y){return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);};
    for(const auto& tri:mesh.Triangles) {
        if(mesh.GroupFlags[tri.Group]&PF_Invisible)continue;
        auto* tex=textures[tri.Group];if(!tex)continue;
        const auto& mips=tex->UncompressedMipmaps.empty()?tex->UsedMipmaps:tex->UncompressedMipmaps;
        if(mips.empty() || tex->Format()!=uint8_t(TextureFormat::P8) || !tex->Palette())throw std::runtime_error("Preview texture format unavailable");
        const auto& mip=mips.front();const auto& palette=tex->Palette()->Colors;
        const auto* replacement=ReviewAtlas(tex);
        const auto &a=vertices[tri.Index[0]],&b=vertices[tri.Index[1]],&c=vertices[tri.Index[2]];
        float area=edge(a,b,c.x,c.y);if(std::abs(area)<.0001f)continue;
        int x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),x1=std::min(Width-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
        int y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))),y1=std::min(Height-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
        auto normal=cross(b-a,c-a);float light=.7f+.3f*std::abs(normal.z)/std::max(.001f,length(normal));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            float u=edge(b,c,x+.5f,y+.5f)/area,v=edge(c,a,x+.5f,y+.5f)/area,w=1-u-v;
            if(u<0 || v<0 || w<0)continue;float z=u*a.z+v*b.z+w*c.z;size_t at=y*Width+x;if(z>=depth[at])continue;
            int tx=std::clamp(int((u*tri.UV[0].x+v*tri.UV[1].x+w*tri.UV[2].x)*mip.Width/255),0,mip.Width-1);
            int ty=std::clamp(int((u*tri.UV[0].y+v*tri.UV[1].y+w*tri.UV[2].y)*mip.Height/255),0,mip.Height-1);
            auto index=mip.Data[ty*mip.Width+tx];if(index>=palette.size() || (!index && (mesh.GroupFlags[tri.Group]&PF_Masked)))continue;
            auto color=palette[index]; // Source is RGBA; Android int pixels are ARGB.
            if(replacement) {
                int rx=std::clamp(int((u*tri.UV[0].x+v*tri.UV[1].x+w*tri.UV[2].x)*replacement->width/255),0,replacement->width-1);
                int ry=std::clamp(int((u*tri.UV[0].y+v*tri.UV[1].y+w*tri.UV[2].y)*replacement->height/255),0,replacement->height-1);
                const auto* rgb=&replacement->rgb[(size_t(ry)*replacement->width+rx)*3];
                color=uint32_t(rgb[0])|(uint32_t(rgb[1])<<8)|(uint32_t(rgb[2])<<16)|0xff000000u;
            }
            pixels[at]=0xff000000u|(uint32_t((color&255)*light)<<16)|(uint32_t(((color>>8)&255)*light)<<8)|uint32_t(((color>>16)&255)*light);depth[at]=z;
        }
    }
    return pixels;
}
}
int Poll(int character,int skin,std::vector<uint32_t>& pixels,bool expanded,float yaw) {
    if(!valid(character,skin))return -1;
    std::lock_guard lock(mutex);auto& card=cards[key(character,skin)+(expanded?37:0)];card.requested=Clock::now();card.yaw=expanded&&std::isfinite(yaw)?std::remainder(yaw,6.2831853f):std::numeric_limits<float>::quiet_NaN();
    if(card.failed)return -1;
    if(card.pixels.empty() || card.delivered==card.generation)return 0;pixels=card.pixels;card.delivered=card.generation;return 1;
}
void Rotate(int character,int skin,float yaw) {
    if(!valid(character,skin) || !std::isfinite(yaw))return;
    std::lock_guard lock(mutex);auto& card=cards[key(character,skin)+37];
    card.yaw=std::remainder(yaw,6.2831853f);card.requested=Clock::now();
}
bool Active() {
    auto now=Clock::now();std::lock_guard lock(mutex);
    return std::any_of(cards.begin(),cards.end(),[&](const Card& c){return !c.failed && now-c.requested<std::chrono::milliseconds(100);});
}
void Tick(Engine& game) {
    auto now=Clock::now();
    std::vector<std::pair<int,float>> requested;
    {std::lock_guard lock(mutex);for(int i=0;i<int(cards.size());++i) {
        auto& c=cards[i];if(now-c.requested>std::chrono::milliseconds(250))std::vector<uint32_t>().swap(c.pixels);
        else if(!c.failed)requested.emplace_back(i,c.yaw);
    }}
    if(requested.empty() || !RuneMenu::Open || !RuneMenu::Covered)return;
    bool expanded=std::any_of(requested.begin(),requested.end(),[](auto request){return request.first>=37;});
    // A recently opened large preview takes priority over stale card requests.
    if(expanded)requested.erase(std::remove_if(requested.begin(),requested.end(),[](auto r){return r.first<37;}),requested.end());
    if(now-lastTick<std::chrono::milliseconds(expanded?8:16))return;lastTick=now;
    if(requested.size()>5)requested.resize(5);
    auto renderStart=Clock::now();
    // Avoid float precision loss at large monotonic-clock uptime.
    float time=float(std::fmod(std::chrono::duration<double>(now.time_since_epoch()).count(),600.0));
    for(auto [i,yaw]:requested)try {
        auto pixels=Render(game,choice(i%37),time,i>=37?Width*2:Width,i>=37?Height*2:Height,yaw);
        std::lock_guard lock(mutex);cards[i].pixels=std::move(pixels);++cards[i].generation;
    }catch(const std::exception& e){std::lock_guard lock(mutex);cards[i].failed=true;LogMessage(std::string("CHARACTER_PREVIEW_FAILED ")+openrune::Characters[choice(i%37).character].name+": "+e.what());}
    static Clock::time_point statsStart{};static int frames=0;static double renderMs=0;static bool statsExpanded=false;
    if(statsExpanded!=expanded || now-statsStart>std::chrono::seconds(6)){statsStart=now;frames=0;renderMs=0;statsExpanded=expanded;}
    ++frames;renderMs+=std::chrono::duration<double,std::milli>(Clock::now()-renderStart).count();
    double elapsed=std::chrono::duration<double>(now-statsStart).count();
    if(elapsed>=5){LogMessage("CHARACTER_PREVIEW_TIMING expanded="+std::to_string(expanded)+" updates_per_sec="+std::to_string(frames/elapsed)+" render_ms="+std::to_string(renderMs/frames));statsStart=now;frames=0;renderMs=0;}
}
void Probe(Engine& game) {
    auto out=std::filesystem::path(commandline->GetArg("","--preview-output",""));
    if(out.empty() || std::filesystem::exists(out))throw std::runtime_error("Use a new preview output directory");
    reviewRoot=commandline->GetArg("","--preview-texture-review","");reviewTextures.clear();
    std::filesystem::create_directories(out);auto start=Clock::now();int frames=0;
    for(int i=0;i<37;++i) {
        auto a=Render(game,choice(i),0),b=Render(game,choice(i),.8f);
        if(a==b || std::count_if(a.begin(),a.end(),[](auto c){return c!=0;})<500)throw std::runtime_error("Empty or static character preview");
        for(int f=0;f<2;++f){auto& pixels=f?b:a;std::ofstream file(out/(std::to_string(i)+"-"+std::to_string(f)+".ppm"),std::ios::binary);file<<"P6\n"<<Width<<" "<<Height<<"\n255\n";
            for(auto c:pixels){unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};file.write(reinterpret_cast<char*>(rgb),3);}++frames;}
    }
    for(int i:{0,11,26,36}) {
        auto pixels=Render(game,choice(i),.4f,Width*2,Height*2);
        if(pixels.size()!=Width*Height*4 || std::count_if(pixels.begin(),pixels.end(),[](auto c){return c!=0;})<2000)throw std::runtime_error("Expanded preview invalid");
        std::ofstream file(out/("expanded-"+std::to_string(i)+".ppm"),std::ios::binary);file<<"P6\n"<<Width*2<<" "<<Height*2<<"\n255\n";
        for(auto c:pixels){unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};file.write(reinterpret_cast<char*>(rgb),3);}
    }
    for(int i=0;i<37;++i) {
        std::vector<uint32_t> previous;
        for(int side=0;side<4;++side) {
            auto pixels=Render(game,choice(i),.4f,Width*2,Height*2,side*1.57079633f);
            if(pixels==previous || std::count_if(pixels.begin(),pixels.end(),[](auto c){return c!=0;})<1000)throw std::runtime_error("Rotated preview invalid");
            if(!reviewRoot.empty()) {
                std::ofstream file(out/("review-"+std::to_string(i)+"-"+std::to_string(side)+".ppm"),std::ios::binary);file<<"P6\n"<<Width*2<<" "<<Height*2<<"\n255\n";
                for(auto c:pixels){unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};file.write(reinterpret_cast<char*>(rgb),3);}
            }
            previous=std::move(pixels);
        }
    }
    std::cout<<"CHARACTER_PREVIEW_ROTATION_OK choices=37 angles=4"<<std::endl;
    std::cout<<"CHARACTER_PREVIEW_EXPANDED_OK choices=4 dimensions=384x448"<<std::endl;
    auto warm=Clock::now();for(int f=0;f<80;++f)Render(game,choice((f%4)*9),f*.05f);
    std::cout<<"CHARACTER_PREVIEW_WARM_MS "<<std::chrono::duration<double,std::milli>(Clock::now()-warm).count()/80<<std::endl;
    std::cout<<"CHARACTER_PREVIEWS_OK choices=37 frames="<<frames<<" total_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<std::endl;
    reviewRoot.clear();reviewTextures.clear();
}
}
