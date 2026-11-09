#include "Precomp.h"
#include "Engine.h"
#include "RuneCharacters.h"
#include "Render/RenderSubsystem.h"
#include "Render/VisibleActor.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Utils/CommandLine.h"
#include <iostream>
#include <set>
#include <chrono>

void RunRuneShadowCullingProbe(Engine& game) {
    // Check analytic clip-space endpoints, independent of the culling oracle.
    // Shadow cameras use near=2; main cameras use near=1 and remain unchanged.
    unsigned depthChecks=0;
    for(float nearPlane:{.5f,1.f,2.f,4.f})for(float farPlane:{64.f,900.f,1800.f})
        for(auto handed:{handedness::left,handedness::right})for(auto range:{clipzrange::zero_positive_w,clipzrange::negative_positive_w}) {
            auto projection=mat4::frustum(-nearPlane,nearPlane,-nearPlane,nearPlane,nearPlane,farPlane,handed,range);
            float sign=handed==handedness::left?1.f:-1.f;
            auto depth=[&](float distance){auto p=projection*vec4(0,0,sign*distance,1);return p.z/p.w;};
            float low=range==clipzrange::zero_positive_w?0.f:-1.f;
            if(std::abs(depth(nearPlane)-low)>.00001f || std::abs(depth(farPlane)-1.f)>.00001f ||
                depth(nearPlane*.5f)>=low || depth(farPlane*1.1f)<=1.f)
                throw std::runtime_error("Shadow projection near/far clip endpoints are wrong");
            depthChecks+=4;
        }
    std::cout<<"SHADOW_PROJECTION_DEPTH_OK checks="<<depthChecks<<std::endl;
    if(commandline->HasArg("","--projection-only"))return;

    auto map=commandline->GetArg("","--rune-shadow-culling-probe","DM-Hildir.run");
    game.NetworkAuthority=true;auto url=game.GetDefaultURL(map);game.LoadMap(url);
    if(commandline->HasArg("","--visibility-bound-audit")) {
        auto* model=game.Level->Model;unsigned checked=0,bad=0,fixedBad=0;
        const auto& fixed=model->ConservativeRenderBounds();
        std::function<BBox(int)> walk=[&](int index) {
            auto& n=model->Nodes[index];BBox all;all.IsValid=false;
            auto add=[&](vec3 p){if(!all.IsValid)all=BBox(p,p);else for(int k=0;k<3;++k){all.min[k]=std::min(all.min[k],p[k]);all.max[k]=std::max(all.max[k],p[k]);}};
            for(int at=index;at>=0;at=model->Nodes[at].Plane) {
                auto& poly=model->Nodes[at];for(int v=0;v<poly.NumVertices;++v)add(model->Points[model->Vertices[poly.VertPool+v].Vertex]);
            }
            for(int child:{n.Front,n.Back})if(child>=0){auto box=walk(child);if(box.IsValid){add(box.min);add(box.max);}}
            if(all.IsValid)for(int k=0;k<3;++k)if(all.min[k]<fixed[index].min[k]-.1f || all.max[k]>fixed[index].max[k]+.1f){++fixedBad;break;}
            if(n.RenderBound>=0 && all.IsValid) {
                ++checked;auto b=model->Bounds[n.RenderBound];bool outside=false;
                for(int k=0;k<3;++k)if(all.min[k]<b.min[k]-.1f || all.max[k]>b.max[k]+.1f)outside=true;
                if(outside){++bad;if(bad<=3)std::cout<<"STATIC_BOUNDS_MISSING node="<<index<<std::endl;}
            }
            return all;
        };walk(0);
        std::cout<<"STATIC_BOUNDS_AUDIT map="<<map<<" checked="<<checked<<" bad="<<bad<<" fixed_bad="<<fixedBad<<std::endl;
        if(fixedBad)throw std::runtime_error("Conservative static bounds lost geometry");
        if(commandline->HasArg("","--visibility-audit-only"))return;
    }
    auto* player=game.LoginNetworkPlayer("Shadow probe");game.viewport->Actor()=player;
    game.render=std::make_unique<RenderSubsystem>(nullptr);
    // Visibility construction uses no GPU state. Draw calls are tested separately.
    for(int i=0;i<30;++i)game.Level->Tick(1.f/60,false);
    auto* npc=UObject::TryCast<UPawn>(player->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},player->Location()+vec3(0,0,150),{}));
    if(!npc)throw std::runtime_error("Shadow probe needs a humanoid");
    npc->SetCollision(false,false,false);npc->SetPhysics(PHYS_None);
    std::vector<vec3> starts;std::vector<UActor*> lights;
    for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe()) {
        if(a->IsA("PlayerStart"))starts.push_back(a->Location());
        if(a->LightType()!=0 && a->LightBrightness()>0 && a->WorldLightRadius()>=64)lights.push_back(a);
    }
    if(starts.empty() || lights.empty())throw std::runtime_error("Map lacks shadow sampling points/lights");
    if(starts.size()>12)starts.resize(12);
    unsigned views=0,wanted=0,missing=0,badBounds=0,reportCount=0;double visibilityMs=0;
    for(auto start:starts) {
        player->Location()=start;
        std::stable_sort(lights.begin(),lights.end(),[&](UActor* a,UActor* b){return length(a->Location()-start)<length(b->Location()-start);});
        for(size_t lightIndex=0;lightIndex<std::min(lights.size(),size_t(6));++lightIndex) {
            auto* light=lights[lightIndex];float radius=std::min(light->WorldLightRadius(),1800.f);
            if(length(light->Location()-start)<8)continue;
            for(int position=0;position<16;++position) {
                float angle=position*3.14159265f/8;float distance=position%2?200.f:90.f;
                npc->Location()=start+vec3(std::cos(angle)*distance,std::sin(angle)*distance,0);
                npc->Rotation()={0,position*4096,0};
                for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe())a->UpdateBspInfo();
                VisibleFrame shadow;auto clock=std::chrono::steady_clock::now();
                shadow.ProcessShadow(light->Location(),start+vec3(0,0,12),radius);
                visibilityMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-clock).count();++views;
                std::set<UActor*> collected;for(const auto& a:shadow.Actors)collected.insert(a.Actor);
                for(auto* actor:{static_cast<UActor*>(player),static_cast<UActor*>(npc)}) {
                    auto oldSize=shadow.Actors.size();VisibleActor oracle;oracle.Process(&shadow,actor);
                    bool expected=shadow.Actors.size()>oldSize;shadow.Actors.resize(oldSize);
                    if(expected) {
                        ++wanted;
                        if(!collected.count(actor)) {
                            ++missing;
                            if(reportCount++<10) {
                                auto* n=actor->BspInfo.Node;
                                std::cout<<"SHADOW_CASTER_MISSING actor="<<actor->Name.ToString()<<" light="<<light->Name.ToString()<<" start="<<start.x<<","<<start.y<<","<<start.z<<" position="<<position<<" node="<<(n?n-&game.Level->Model->Nodes[0]:-1)<<" bound="<<(n?n->RenderBound:-1)<<std::endl;
                            }
                        }
                    }
                }
                auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(npc));
                model->PrepareMesh(npc->GetByte("SkelMesh"),npc->AnimSequence(),npc->AnimFrame(),npc);
                auto transform=GetRuneMeshToWorld(npc);auto box=npc->BspInfo.BoundingBox;bool outside=false;
                for(auto vertex:model->Verts) {
                    auto p=(transform*vec4(vertex,1)).xyz();
                    for(int axis=0;axis<3;++axis)if(p[axis]<box.min[axis]-.05f || p[axis]>box.max[axis]+.05f)outside=true;
                }
                badBounds+=outside;
            }
        }
    }
    unsigned rosterPoses=0,rosterFailures=0;
    if(commandline->HasArg("","--shadow-roster-bounds"))for(const auto& character:openrune::Characters) {
        auto* prototype=game.packages->FindClass(character.className)->GetDefaultObject<UActor>();
        auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(prototype));if(!model)continue;
        npc->SetObject("Skeletal",model);npc->DrawScale()=prototype->DrawScale();
        for(size_t variant=0;variant<model->Meshes.size();++variant) {
            npc->SetByte("SkelMesh",uint8_t(variant));
            for(size_t sequence=0;sequence<model->Sequences.size();sequence+=std::max(size_t(1),model->Sequences.size()/8))
                for(float phase:{0.f,.23f,.57f,.91f}) {
                    npc->AnimSequence()=model->Sequences[sequence].Info.Name;npc->AnimFrame()=phase;
                    npc->UpdateBspInfo();model->PrepareMesh(int(variant),npc->AnimSequence(),phase,npc);
                    auto world=GetRuneMeshToWorld(npc);auto box=npc->BspInfo.BoundingBox;bool failed=false;
                    for(auto vertex:model->Verts) {
                        auto p=(world*vec4(vertex,1)).xyz();
                        for(int k=0;k<3;++k)if(p[k]<box.min[k]-.05f || p[k]>box.max[k]+.05f)failed=true;
                    }
                    ++rosterPoses;rosterFailures+=failed;
                }
        }
    }
    std::cout<<"RUNE_SHADOW_CULLING_RESULT map="<<map<<" views="<<views<<" expected_casters="<<wanted<<" missing="<<missing<<" mesh_bounds_failures="<<badBounds<<" roster_poses="<<rosterPoses<<" roster_bounds_failures="<<rosterFailures<<" mean_visibility_ms="<<visibilityMs/views<<std::endl;
    if(commandline->HasArg("","--shadow-culling-strict") && (missing || badBounds || rosterFailures))throw std::runtime_error("Shadow visibility omitted casters or mesh bounds");
}
