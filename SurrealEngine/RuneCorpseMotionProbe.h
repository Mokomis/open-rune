#pragma once
// Runs inside the isolated native corpse fixture, without changing gameplay ticks.
template<class Check>
void CheckRuneCorpseMotion(Engine& game,UPawn* carrier,UActor* body,Check check) {
    auto initial=carrier->Location();auto initialRotation=carrier->Rotation();
    auto* original=body->GetUObject("Skeletal");auto variant=body->GetByte("SkelMesh");auto scale=body->DrawScale();
    auto beforeLocation=body->Location(),beforeVelocity=body->Velocity();auto life=body->LifeSpan();
    float peak=0,worstRateError=0;unsigned tested=0,motionFrames=0;double motionMicros=0;
    auto simulate=[&](int hz,float seconds) {
        RuneCorpse::ResetMotion(body);carrier->Location()=initial;carrier->Rotation()=initialRotation;
        RuneCorpse::TickMotion(body,1.f/120);auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(body));
        auto base=m->JointMatrices(body->CorpseBody.pose);
        check(body->CorpseBody.limbs.size()>=5,"humanoid has pendulum chains");
        for(int frame=1;frame<=int(hz*seconds);++frame) {
            float t=float(frame)/hz;
            carrier->Location()=initial+vec3(std::sin(t*4)*24,std::sin(t*3)*18,std::sin(t*5)*3);
            carrier->Rotation()=initialRotation;carrier->Rotation().Yaw+=int(std::sin(t*2)*6000);
            auto start=std::chrono::steady_clock::now();RuneCorpse::TickMotion(body,1.f/hz);
            motionMicros+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();++motionFrames;
            auto& s=body->CorpseBody;auto posed=m->JointMatrices(s.motionPose);
            if(frame%15==0) {
                body->UpdateBspInfo();auto bounds=body->BspInfo.BoundingBox;auto world=GetRuneMeshToWorld(body);
                auto* groups=static_cast<int*>(body->GetProperty("SkelGroupFlags"));
                for(const auto& tri:m->Tris)if(!(groups[tri.TextureIndex]&PF_Invisible))for(auto i:tri.Indices) {
                    auto v=(world*vec4(m->Verts[i],1)).xyz();
                    for(int k=0;k<3;++k)check(v[k]>=bounds.min[k]-.02f && v[k]<=bounds.max[k]+.02f,"moving limb stays inside culling bounds");
                }
            }
            for(size_t j=0;j<s.pose.size();++j) {
                check(s.pose[j].Position==s.motionPose[j].Position && s.pose[j].Scale==s.motionPose[j].Scale,"flop retains reference lengths and scales");
                auto point=[](const mat4& b){return vec3(b[12],b[13],b[14]);};
                if(m->Joints[j].Parent>=0) {
                    int parent=m->Joints[j].Parent;
                    check(std::abs(length(point(posed[j])-point(posed[parent]))-length(point(base[j])-point(base[parent])))<.003f,"flop retains world bone lengths");
                }
                if(m->Joints[j].Name=="chest" || m->Joints[j].Name=="pelvis")check(length(point(posed[j])-point(base[j]))<.001f,"torso support stays fixed");
                peak=std::max(peak,length(point(posed[j])-point(base[j])));
                for(int c=0;c<16;++c)check(std::isfinite(posed[j][c]),"finite pendulum pose");
            }
            if(frame==hz) {
                auto cached=s.motionPose;auto tip=s.limbs.front().tip;
                m->ActorJointMatrices(body);m->ActorJointMatrices(body);RuneCorpse::TickMotion(body,0);
                check(tip==s.limbs.front().tip,"render queries and pause cannot tick dynamics");
                check(cached[0].Rotation==s.motionPose[0].Rotation,"root pose immutable during draw");
            }
        }
        auto bones=m->JointMatrices(body->CorpseBody.motionPose);std::vector<vec3> result;
        for(const auto& n:body->CorpseBody.limbs)result.emplace_back(bones[n.child][12],bones[n.child][13],bones[n.child][14]);
        return result;
    };
    auto reference=simulate(120,2);
    for(int hz:{30,60,144,240}) {
        auto at=simulate(hz,2);check(at.size()==reference.size(),"rate-independent chain topology");
        for(size_t j=0;j<at.size();++j)worstRateError=std::max(worstRateError,length(at[j]-reference[j]));
    }
    check(peak>2,"limbs actually articulate with movement");check(worstRateError<2,"bounded pose error across presentation rates");
    // Every selectable skeleton/variant keeps its actual proportions and torso.
    for(const auto& character:openrune::Characters) {
        auto* prototype=game.packages->FindClass(character.className)->template GetDefaultObject<UActor>();
        body->SetObject("Skeletal",GetRuneDisplayMesh(prototype));body->SetByte("SkelMesh",prototype->GetByte("SkelMesh"));body->DrawScale()=prototype->DrawScale();body->CorpseBody.centered=false;
        simulate(60,.15f);++tested;
    }
    body->SetObject("Skeletal",original);body->SetByte("SkelMesh",variant);body->DrawScale()=scale;body->CorpseBody.centered=false;
    simulate(60,.1f);carrier->Location()+=vec3(4096,0,0);RuneCorpse::TickMotion(body,1.f/60);
    check(body->CorpseBody.motionReady,"teleport restarts presentation safely");
    for(const auto& n:body->CorpseBody.limbs)check(std::isfinite(length(n.velocity)) && length(n.tip-n.anchor)<100,"teleport cannot explode chains");
    check(body->Location()==beforeLocation && body->Velocity()==beforeVelocity && body->LifeSpan()==life,"cosmetic motion cannot modify body physics");
    carrier->Location()=initial;carrier->Rotation()=initialRotation;RuneCorpse::ResetMotion(body);body->CorpseBody.centered=false;
    std::cout<<"RUNE_CORPSE_MOTION_OK models="<<tested<<" peak="<<peak<<" rate_error="<<worstRateError<<" mean_us="<<motionMicros/motionFrames<<std::endl;
}
