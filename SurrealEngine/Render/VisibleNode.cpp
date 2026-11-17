
#include "Precomp.h"
#include "VisibleNode.h"
#include "VisibleDecal.h"
#include "RenderSubsystem.h"
#include "Engine.h"
#include "Package/Package.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Collision/TopLevel/CollisionHit.h"

void VisibleNode::Draw(VisibleFrame* frame)
{
	UModel* model = engine->Level->Model;
	BspSurface& surface = model->Surfaces[Node->Surf];

	const vec3& UVec = model->Vectors[surface.vTextureU];
	const vec3& VVec = model->Vectors[surface.vTextureV];
	const vec3& Base = model->Points[surface.pBase];

	engine->render->UpdateTexture(surface.Material);

	auto zoneActor = engine->GetZoneActor(Front ? Node->Zone1 : Node->Zone0);

	// If no ZoneInfo is found, use the values from LevelInfo instead.
	float ZoneUPanSpeed = zoneActor->TexUPanSpeed();
	float ZoneVPanSpeed = zoneActor->TexVPanSpeed();

	UTexture* tex;
	if (surface.Material)
		tex = surface.Material->GetAnimTexture();
	else
		tex = engine->LevelInfo->DefaultTexture();

	TextureInfo texture;
	engine->render->UpdateTexture(tex);
	engine->render->UpdateTextureInfo(texture, surface, tex, ZoneUPanSpeed, ZoneVPanSpeed);

	TextureInfo detailtex;
	if (surface.Material && surface.Material->DetailTexture())
	{
		tex = surface.Material->DetailTexture()->GetAnimTexture();
		engine->render->UpdateTexture(tex);
		engine->render->UpdateTextureInfo(detailtex, surface, tex, ZoneUPanSpeed, ZoneVPanSpeed);
	}

	TextureInfo macrotex;
	if (surface.Material && surface.Material->MacroTexture())
	{
		tex = surface.Material->MacroTexture()->GetAnimTexture();
		engine->render->UpdateTexture(tex);
		engine->render->UpdateTextureInfo(macrotex, surface, tex, ZoneUPanSpeed, ZoneVPanSpeed);
	}

	int numverts = Node->NumVertices;
	vec3* points = engine->render->GetTempVertexBuffer(numverts);

	BspVert* v = &model->Vertices[Node->VertPool];
	if (frame->MirrorFlag)
	{
		for (int j = 0; j < numverts; j++)
		{
			points[numverts - 1 - j] = model->Points[v[j].Vertex];
		}
	}
	else
	{
		for (int j = 0; j < numverts; j++)
		{
			points[j] = model->Points[v[j].Vertex];
		}
	}

	TextureInfo lightmap;
	TextureInfo fogmap;
	if (!frame->ShadowView && (PolyFlags & PF_Unlit) == 0)
	{
		lightmap = engine->Level->Light.GetLevelLightmap(surface, zoneActor, model);
		fogmap = engine->Level->Light.GetLevelFogmap(surface, engine->GetZoneActor(frame->ViewZone), model);
	}

	SurfaceInfo surfaceinfo;
	surfaceinfo.PolyFlags = PolyFlags;
	surfaceinfo.Texture = &texture;
	surfaceinfo.MacroTexture = surface.Material && surface.Material->MacroTexture() ? &macrotex : nullptr;
	surfaceinfo.DetailTexture = surface.Material && surface.Material->DetailTexture() ? &detailtex : nullptr;
	surfaceinfo.LightMap = lightmap.NumMips != 0 ? &lightmap : nullptr;
	surfaceinfo.FogMap = fogmap.NumMips != 0 ? &fogmap : nullptr;

	SurfaceFacet facet;
	float waterHeight=0; facet.Water=(frame->Device->WorldFeatures&2048) && WaterPlane(waterHeight);
	facet.MapCoords.Origin = Base;
	facet.MapCoords.XAxis = UVec;
	facet.MapCoords.YAxis = VVec;
	facet.Vertices = points;
	facet.VertexCount = numverts;
    if(facet.Water && frame->Device->UsesModernWater()) {
        auto& waterDepth=engine->render->WaterFaceDepth[{Node,frame->MirrorFlag}];
        if(waterDepth.empty()) {
        waterDepth.reserve(numverts);
        auto& cache=engine->render->WaterDepthSamples;
        for(int i=0;i<numverts;++i) {
            // Shared BSP corners must sample the same world position. Moving
            // toward each face's centre gave the same edge different depths.
            std::array<int,3> key={int(std::lround(points[i].x*4)),int(std::lround(points[i].y*4)),int(std::lround(points[i].z*4))};
            auto found=cache.find(key);
            if(found==cache.end()) {
                const vec3 sample(float(key[0])*.25f,float(key[1])*.25f,float(key[2])*.25f);
                std::array<float,5> depths{};int count=0;
                // Boundary vertices can lie inside a wall. Use a fixed,
                // face-independent footprint; reject solid starts/missed floors
                // instead of treating them as very shallow/deep water.
                for(vec2 offset:{vec2(0,0),vec2(8,0),vec2(-8,0),vec2(0,8),vec2(0,-8)}) {
                    vec3 start=sample+vec3(offset.x,offset.y,-1);
                    TraceFlags flags;flags.world=true;
                    auto hit=engine->Level->Collision.TraceFirstHit(start,start-vec3(0,0,2048),nullptr,vec3(0),flags);
                    if(hit.Actor && hit.Fraction>0 && hit.Fraction<1 && hit.Normal.z>.1f)
                        depths[count++]=std::clamp(hit.Fraction*2048.f+1.f,1.f,2048.f);
                }
                std::sort(depths.begin(),depths.begin()+count);
                found=cache.emplace(key,count?depths[(count-1)/2]:2048.f).first;
            }
            waterDepth.push_back(found->second);
        }
        }
        facet.WaterDepth=waterDepth.data();
    }
    auto materialPackage = texture.Texture && texture.Texture->package ? texture.Texture->package->GetPackageName() : NameString();
    bool materialRelief = materialPackage == "Wood" || materialPackage == "Rock" || materialPackage == "Brckston";
    if (!frame->ShadowView && (frame->Device->WorldFeatures & 2) && materialRelief && numverts > 0 && surface.LightMap >= 0 && surface.LightMap < (int)model->LightMap.size())
    {
        vec3 center(0.0f); for(int i=0;i<numverts;i++) center+=points[i]; center/=float(numverts);
        int first=model->LightMap[surface.LightMap].LightActors;
        if(first>=0) for(int i=first;i<(int)model->Lights.size() && i<first+32;i++)
        {
            UActor* light=model->Lights[i]; if(!light) break;
            if(light->LightType()==LT_None || light->LightBrightness()==0) continue;
            vec3 delta=light->Location()-center;float dist2=dot(delta,delta);
            float radius=light->WorldLightRadius();
            if(dist2>1.0f && dist2<radius*radius)
                facet.ReliefLightDirection+=delta*(light->LightBrightness()*(1.0f-dist2/(radius*radius))/std::sqrt(dist2));
        }
    }

	frame->Device->DrawComplexSurface(&frame->Frame, surfaceinfo, facet);
	engine->render->Stats.Surfaces++;

	if (frame->ShadowView) return;
	VisibleDecal decal;
	decal.DrawDecals(frame, Node);
}

// Only horizontal water surfaces qualify; lava, slime, waterfalls
// and moving brush geometry retain their authored rendering.
bool VisibleNode::WaterPlane(float& height) const
{
    if(!Node || Node->NumVertices<3 || (PolyFlags&(PF_Invisible|PF_FakeBackdrop|PF_Masked)))return false;
    auto* model=engine->Level->Model;
    auto& surface=model->Surfaces[Node->Surf];
    vec3 normal=model->Vectors[surface.vNormal];
    if(std::abs(normal.z)<0.99f || !surface.Material)return false;
    auto& cache=engine->render->WaterMaterialKinds;
    auto found=cache.find(surface.Material);
    if(found==cache.end()) {
        std::string name=surface.Material->Name.ToString();
        if(surface.Material->package)name+=" "+surface.Material->package->GetPackageName().ToString();
        for(char& c:name)c=(char)std::tolower((unsigned char)c);
        int kind=(name.find("water")!=std::string::npos || surface.Material->IsA("WaterTexture"))?2:1;
        for(const char* bad:{"lava","slime","blood","acid"})if(name.find(bad)!=std::string::npos)kind=0;
        found=cache.emplace(surface.Material,kind).first;
    }
    if(found->second==0)return false;
    if(found->second==1) {
        // A solid riverbed/ceiling bordering a water zone is not a water plane.
        if(!(PolyFlags&(PF_Translucent|PF_Modulated)))return false;
        auto* z0=engine->GetZoneActor(Node->Zone0);auto* z1=engine->GetZoneActor(Node->Zone1);
        if(!z0 || !z1 || z0->bWaterZone()==z1->bWaterZone())return false;
    }
    height=model->Points[surface.pBase].z;return true;
}
