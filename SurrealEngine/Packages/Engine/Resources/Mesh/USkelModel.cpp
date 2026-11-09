#include "Precomp.h"
#include "RunePhysics.h"
#include "RuneSlam.h"
#include "RuneBash.h"
#include "USkelModel.h"
#include "RuneDualWield.h"
#include "RuneRushPose.h"
#include "Package/ObjectStream.h"
#include "Math/coords.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Engine.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include <algorithm>
#include <cmath>

// Format and Euler conversion reference: UE Viewer, UnMeshRune.cpp (MIT),
// copyright 2022 Konstantin Nosov. See Docs/RuneModel-THIRD-PARTY.txt.
// Checked against Rune Classic 1.11 native Serialize and DecodeFrame evidence.
namespace
{
int Count(ObjectStream* s, uint32_t minimumBytes = 1)
{
	int n = s->ReadIndex();
	if (n < 0 || (uint64_t)n * minimumBytes > s->RemainingBytes())
		Exception::Throw("Invalid Rune model array count");
	return n;
}
vec3 Vector(ObjectStream* s) { float x=s->ReadFloat(), y=s->ReadFloat(), z=s->ReadFloat(); return {x,y,z}; }
Rotator Rotation(ObjectStream* s) { int p=s->ReadInt32(), y=s->ReadInt32(), r=s->ReadInt32(); return {p,y,r}; }
BBox Box(ObjectStream* s) { BBox b; b.min=Vector(s); b.max=Vector(s); b.IsValid=s->ReadUInt8()!=0; return b; }
Array<uint8_t> Bytes(ObjectStream* s)
{
	Array<uint8_t> b; b.resize(Count(s)); s->ReadBytes(b.data(), (uint32_t)b.size()); return b;
}

}

UMesh* GetRuneDisplayMesh(UActor* actor)
{
	// Rune's execPlayAnim/execLoopAnim resolve an AnimationProxy sequence
	// against its owner's skeleton, while keeping time on the proxy itself.
	if (engine->LaunchInfo.IsRune() && actor->IsA("AnimationProxy"))
	{
		auto* owner=actor->Owner();
		return owner ? UObject::TryCast<USkelModel>(owner->GetUObject("Skeletal")) : nullptr;
	}
	if (engine->LaunchInfo.IsRune() && actor->HasProperty("Skeletal"))
	{
		auto* skeletal=UObject::TryCast<USkelModel>(actor->GetUObject("Skeletal"));
		if (skeletal) return skeletal;
	}
	return actor->Mesh();
}

USkelModel* GetRuneGeometry(UActor* actor)
{
    if(actor && engine->LaunchInfo.IsRune() && actor->HasProperty("SubstituteMesh"))
        if(auto* substitute=UObject::TryCast<USkelModel>(actor->GetUObject("SubstituteMesh")))return substitute;
    return actor?UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor)):nullptr;
}

Array<mat4> USkelModel::ActorJointMatrices(UActor* actor) const
{
    if(RuneAnimation::Interpolate) return JointMatrices(ActorPose(actor));
	NameString name=actor->AnimSequence().IsNone()?actor->RuneResetSequence:actor->AnimSequence();
	size_t sequence=0;
	for(size_t i=0;i<Sequences.size();i++) if(Sequences[i].Info.Name==name){sequence=i;break;}
	int frame=(int)(std::max(0.0f,actor->AnimFrame())*Sequences[sequence].Info.NumFrames)%Sequences[sequence].Info.NumFrames;
	return JointMatrices(DecodeFrame(sequence,frame,false,actor));
}

BBox USkelModel::PoseBounds(UActor* actor) const
{
    const auto* geometry=GetRuneGeometry(actor);
    if(!geometry || geometry->Meshes.empty())return BoundingBox;
    int variant=actor->GetByte("SkelMesh");if(variant>=int(geometry->Meshes.size()))variant=0;
    auto& cache=geometry->InfluenceBoxes;
    if(cache.size()!=geometry->Meshes.size())cache.resize(geometry->Meshes.size());
    auto& boxes=cache[variant];
    if(boxes.empty()) {
        boxes.resize(Joints.size());for(auto& box:boxes)box.IsValid=false;
        auto include=[&](int joint,vec3 point) {
            if(joint<0 || joint>=int(boxes.size()))return;
            auto& box=boxes[joint];
            if(!box.IsValid){box=BBox(point,point);return;}
            for(int k=0;k<3;++k){box.min[k]=std::min(box.min[k],point[k]);box.max[k]=std::max(box.max[k],point[k]);}
        };
        for(const auto& vertex:geometry->Meshes[variant].Vertices) {
            if(vertex.Weight>0)include(vertex.Joint[0],vertex.Point[0]);
            if(vertex.Weight<1)include(vertex.Joint[1],vertex.Point[1]);
        }
    }
    auto bones=ActorJointMatrices(actor);BBox result;result.IsValid=false;
    for(size_t j=0;j<boxes.size() && j<bones.size();++j)if(boxes[j].IsValid) {
        auto box=boxes[j].transform(bones[j]);
        if(!result.IsValid)result=box;
        else for(int k=0;k<3;++k){result.min[k]=std::min(result.min[k],box.min[k]);result.max[k]=std::max(result.max[k],box.max[k]);}
    }
    return result.IsValid?result:BoundingBox;
}

void USkelModel::InitializeDynamics(UActor* actor) const
{
	auto* flags=static_cast<uint8_t*>(actor->GetProperty("JointFlags"));
	bool needed=false;
	for(size_t j=0;j<Joints.size() && j<50;j++) if(flags[j]&0xe6) { needed=true; break; }
	if(!needed) { actor->RuneDynamics.clear(); actor->RuneDynamicModel=this; return; }
	bool reset=actor->RuneDynamicModel!=this || actor->RuneDynamics.size()!=Joints.size();
	if(reset) { actor->RuneDynamics.clear(); actor->RuneDynamics.resize(Joints.size()); actor->RuneDynamicModel=this; }
	NameString name=actor->AnimSequence().IsNone()?actor->RuneResetSequence:actor->AnimSequence();
	size_t sequence=0;
	for(size_t i=0;i<Sequences.size();i++) if(Sequences[i].Info.Name==name) { sequence=i; break; }
	Array<RuneJointPose> initial;
	for(size_t j=0;j<Joints.size() && j<50;j++)
	{
		auto& state=actor->RuneDynamics[j]; uint8_t kind=flags[j]&0xe6;
		if(!reset && state.Flags==kind) continue;
		state={}; state.Flags=kind;
		if(!kind) continue;
		if(initial.empty()) initial=DecodeFrame(sequence,0);
		if(kind&6) { state.Position=initial[j].Position; state.Rotation=initial[j].Rotation; }
		if(kind&4) state.Target=state.Position;
	}
}

void USkelModel::ApplyDynamicPose(UActor* actor, Array<RuneJointPose>& pose) const
{
	InitializeDynamics(actor);
	if(actor->RuneDynamics.empty()) return;
	// Recovered DynamicPosition/Rotation and ApplyDynamicPose: spring points
	// replace local positions; accelerators bend their parent instead of moving
	// the child endpoint independently. There is no dynamic scale operation.
	for(size_t j=0;j<pose.size() && j<50;j++)
	{
		auto& state=actor->RuneDynamics[j]; uint8_t flags=state.Flags;
		if(flags&0x20) pose[j].Position=state.Position;
		else if(flags&0x80) { state.Target=vec3(0.0f); state.Length=length(pose[j].Position); }
		else if(flags&2)
		{
			state.Length=length(pose[j].Position);
			float magnitude=5000.0f;
			if(auto* function=FindEventFunction(actor,"GetAccelJointMagnitude"))
			{
				Array<ExpressionValue> args={ExpressionValue::IntValue((int)j)};
				magnitude=Frame::Call(function,actor,{args.data(),args.size()}).ToFloat();
			}
			state.Target=state.Length>1e-8f && std::isfinite(magnitude)?pose[j].Position*(magnitude/state.Length):vec3(0.0f);
		}
		else if(flags&4) { state.Target=pose[j].Position; pose[j].Position=state.Position; }
		if(flags&0x40) pose[j].Rotation=state.Rotation;
		else if(!(flags&0x82) && (flags&4)) { state.RotationTarget=pose[j].Rotation; pose[j].Rotation=state.Rotation; }
	}
	for(size_t j=0;j<pose.size();j++) for(int child:Joints[j].Children)
	{
		if(child<0 || child>=50 || child>=(int)actor->RuneDynamics.size()) continue;
		const auto& state=actor->RuneDynamics[child];
		if(!(state.Flags&0x82)) continue;
		Rotator rest=Rotator::FromVector(state.Target), displaced=Rotator::FromVector(state.Position);
		pose[j].Rotation.Pitch+=displaced.Roll-rest.Roll;
		pose[j].Rotation.Yaw+=displaced.Yaw-rest.Yaw;
		int pitch=displaced.Pitch-rest.Pitch;
		pose[j].Rotation.Roll+=rest.Yaw>0?-pitch:pitch;
	}
}

void USkelModel::ApplyJointForce(UActor* actor, int joint, const vec3& force) const
{
	if(joint<0 || joint>=50 || joint>=(int)Joints.size() || !std::isfinite(force.x) || !std::isfinite(force.y) || !std::isfinite(force.z)) return;
	auto* flags=static_cast<uint8_t*>(actor->GetProperty("JointFlags"));
	if(!(flags[joint]&0x86)) return;
	InitializeDynamics(actor);
	mat3 world(GetRuneMeshToWorld(actor)*ActorJointMatrices(actor)[joint]);
	if(std::abs(mat3::determinant(world))<1e-12) return;
	// execApplyJointForce adds the world-to-joint vector to velocity, not to
	// acceleration. The next skeleton tick integrates the impulse.
	vec3 velocity=actor->RuneDynamics[joint].Velocity+mat3::inverse(world)*force;
	if(std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z)) actor->RuneDynamics[joint].Velocity=velocity;
}

void TickRuneDynamics(UActor* actor, float elapsed)
{
	if(!engine->LaunchInfo.IsRune() || actor->bDeleteMe() || actor->IsA("AnimationProxy")) return;
	if(auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor))) model->TickDynamics(actor,elapsed);
}

void USkelModel::TickDynamics(UActor* actor, float elapsed) const
{
	if(!std::isfinite(elapsed) || elapsed<=0) return;
	InitializeDynamics(actor);
	if(actor->RuneDynamics.empty()) return;
	// Refresh animation targets and the current joint coordinate frames before
	// integrating; this also makes impulses independent of render visibility.
	auto bones=ActorJointMatrices(actor);
	mat4 world=GetRuneMeshToWorld(actor);
	for(size_t j=0;j<actor->RuneDynamics.size() && j<50;j++)
	{
		auto& state=actor->RuneDynamics[j]; if(!(state.Flags&0x86)) continue;
		mat3 jointWorld(world*bones[j]);
		if(std::abs(mat3::determinant(jointWorld))<1e-12) continue;
		mat3 worldToJoint=mat3::inverse(jointWorld);
		Array<ExpressionValue> parms={ExpressionValue::IntValue((int)j),ExpressionValue::FloatValue(1),ExpressionValue::FloatValue(2000)};
		if(state.Flags&0x82)
		{
			if(auto* function=FindEventFunction(actor,"GetAccelJointParms")) Frame::Call(function,actor,{parms.data(),parms.size()});
			float damping=parms[1].ToFloat(), threshold=parms[2].ToFloat();
			if(!std::isfinite(damping) || !std::isfinite(threshold)) continue;
			vec3 previous=state.Position;
			state.Velocity+=state.Target*elapsed;
			state.Position+=state.Velocity*elapsed;
			if(!(state.Flags&0x80) && actor->GetBool("bApplyLagToAccelerators")) state.Position+=worldToJoint*(-actor->Velocity())*elapsed;
			float magnitude=length(state.Position);
			if(magnitude>1e-8f) state.Position*=state.Length/magnitude;
			state.Velocity=(state.Position-previous)*((1.0f-damping)/elapsed);
			if(!(state.Flags&0x80))
			{
				float denom=length(state.Position)*length(state.Target);
				if(denom>1e-8f && dot(state.Position,state.Target)/denom<std::cos(threshold*(3.14159265358979323846f/32768.0f))) state.Velocity=vec3(0.0f);
			}
		}
		else if(state.Flags&4)
		{
			parms.push_back(ExpressionValue::VectorValue(vec3(0.0f)));
			if(auto* function=FindEventFunction(actor,"GetSpringJointParms")) Frame::Call(function,actor,{parms.data(),parms.size()});
			float damping=parms[1].ToFloat(), spring=parms[2].ToFloat();
			vec3 threshold=worldToJoint*parms[3].ToVector();
			if(!std::isfinite(damping) || !std::isfinite(spring)) continue;
			vec3 previous=state.Position-state.Target;
			state.Velocity-=previous*(spring*elapsed);
			float drag=damping*elapsed;
			state.Velocity=std::abs(drag)<=1.0f?state.Velocity*(1.0f-drag):vec3(0.0f);
			state.Position+=state.Velocity*elapsed;
			state.RotationVelocity+=(state.RotationTarget-state.Rotation)*(spring*elapsed);
			state.Rotation+=state.RotationVelocity*elapsed;
			vec3 offset=state.Position-state.Target;
			for(int axis=0;axis<3;axis++) offset[axis]=std::clamp(offset[axis],-std::abs(threshold[axis]),std::abs(threshold[axis]));
			state.Position=state.Target+offset;
			if(length(offset)<0.1f && dot(state.Velocity,state.Velocity)<0.1f) { state.Position=state.Target; state.Velocity=vec3(0.0f); offset=vec3(0.0f); }
			vec3 changed(0.0f);
			for(int axis=0;axis<3;axis++) if(previous[axis]*offset[axis]<0) changed[axis]=offset[axis];
			if(changed!=vec3(0.0f)) CallEvent(actor,"JointChangedDirection",{ExpressionValue::IntValue((int)j),ExpressionValue::VectorValue(jointWorld*state.Velocity),ExpressionValue::VectorValue(jointWorld*changed)});
		}
	}
}

mat4 GetRuneActorToWorld(UActor* actor,int depth)
{
	if(depth>16) Exception::Throw("Rune attachment cycle or excessive nesting");
	if(engine->LaunchInfo.IsRune() && actor->bCarriedItem())
	{
		auto* parent=UObject::TryCast<UActor>(actor->GetUObject("AttachParent"));
		auto* model=parent?UObject::TryCast<USkelModel>(GetRuneDisplayMesh(parent)):nullptr;
		int joint=actor->GetByte("AttachParentJoint");
		if(model && joint<(int)model->Joints.size())
			return GetRuneMeshToWorld(parent,depth+1)*RuneDualWield::Attachment(*model,parent,actor,model->ActorJointMatrices(parent))*mat4::scale(actor->DrawScale());
	}
	return mat4::translate(actor->Location()+actor->PrePivot()+vec3(0,0,actor->NetworkReplica?actor->NetworkStairOffset:0))*Coords::Rotation(actor->Rotation()).ToMatrix()*mat4::scale(actor->DrawScale());
}

mat4 GetRuneMeshToWorld(UActor* actor,int depth)
{
    if(engine->LaunchInfo.IsRune() && RunePhysics::CustomTransform(actor))return RunePhysics::MeshToWorld(actor);
    if(engine->LaunchInfo.IsRune() && RuneCorpse::CustomTransform(actor))return RuneCorpse::MeshToWorld(actor);
	auto* mesh=GetRuneDisplayMesh(actor);
	mat4 actorToWorld=GetRuneActorToWorld(actor,depth);
	if (!mesh) return actorToWorld;
	if (auto* model=UObject::TryCast<USkelModel>(mesh); model && !actor->bCarriedItem())
	{
		// Original SetAnimFrame adds PositionOffset to Location + PrePivot
		// before either rotation or DrawScale. UEViewer's negated mesh origin
		// is a conversion convention, not the original actor placement.
		return mat4::translate(model->PositionOffset)*actorToWorld*Coords::Rotation(model->RotationOffset).ToMatrix();
	}
	// Carried items use their parent's joint frame; retain their distinct
	// local mesh conversion rather than adding a world-space actor offset.
	return actorToWorld*mesh->meshToObject;
}

void UpdateRuneAttachments(UActor* actor)
{
	if(!engine->LaunchInfo.IsRune()) return;
	auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));
	if(!model || actor->IsA("AnimationProxy")) return;
	auto* children=static_cast<UObject**>(actor->GetProperty("JointChild"));
	bool hasChildren=false;
	for(size_t j=0;j<model->Joints.size() && j<50;j++) if(children[j]){hasChildren=true;break;}
	if(!hasChildren) return;
	auto bones=model->ActorJointMatrices(actor);
	mat4 world=GetRuneMeshToWorld(actor);
	for(size_t j=0;j<model->Joints.size() && j<50;j++)
	{
		auto* child=UObject::TryCast<UActor>(children[j]);
		if(!child || child->bDeleteMe() || child->GetUObject("AttachParent")!=actor) continue;
		vec3 position=(world*RuneDualWield::Attachment(*model,actor,child,bones)*vec4(0,0,0,1)).xyz();
        if(RuneCorpse::CustomTransform(child))position=(RuneCorpse::MeshToWorld(child)*vec4(child->CorpseBody.center,1)).xyz();
		// TickSkeleton uses FarMoveActor with collision placement checks disabled.
		child->XLevel()->Collision.RemoveFromCollision(child);
		child->Location()=position;
		child->XLevel()->Collision.AddToCollision(child);
	}
}

void USkelModel::Load(ObjectStream* s)
{
    LegMirrorBasis.clear();
	UPrimitive::Load(s);
	int numMeshes=s->ReadInt32(), numJoints=s->ReadInt32(), numFrames=s->ReadInt32(), numSequences=s->ReadInt32();
	NumSkins=s->ReadInt32(); RootJoint=s->ReadInt32();
	if (numMeshes<0 || numJoints<=0 || numFrames<0 || numSequences<=0 || RootJoint<0 || RootJoint>=numJoints)
		Exception::Throw("Invalid Rune model header");
	Meshes.resize(Count(s, 10));
	for (auto& m : Meshes)
	{
		int nv=s->ReadInt32(), nt=s->ReadInt32();
		m.Triangles.resize(Count(s, 13));
		for (auto& t : m.Triangles)
		{
			for (auto& v:t.Index) v=s->ReadUInt16();
			for (auto& uv:t.UV) { uv.x=s->ReadUInt8(); uv.y=s->ReadUInt8(); }
			t.Group=s->ReadUInt8();
			if (t.Group>=16) Exception::Throw("Invalid Rune polygroup");
		}
		m.Vertices.resize(Count(s, 36));
		for (auto& v:m.Vertices)
		{
			v.Point[0]=Vector(s); v.Point[1]=Vector(s);
			v.Joint[0]=s->ReadInt32(); v.Joint[1]=s->ReadInt32(); v.Weight=s->ReadFloat();
			if (v.Joint[0]<0 || v.Joint[0]>=numJoints || !std::isfinite(v.Weight) || v.Weight<0 || v.Weight>1 ||
				(v.Weight!=1 && (v.Joint[1]<0 || v.Joint[1]>=numJoints)))
				Exception::Throw("Invalid Rune vertex influence");
		}
		if (nv!=(int)m.Vertices.size() || nt!=(int)m.Triangles.size()) Exception::Throw("Rune mesh count mismatch");
		for (const auto& t:m.Triangles) for (auto v:t.Index)
			if (v>=m.Vertices.size()) Exception::Throw("Rune triangle index out of range");
		m.DecimationCount=s->ReadInt32(); m.DecimationData=Bytes(s);
		for (int i=0;i<16;i++) { m.GroupFlags[i]=s->ReadInt32(); m.SkinNames[i]=s->ReadName(); }
	}
	Joints.resize(Count(s, 137));
	for (size_t i=0;i<Joints.size();i++)
	{
		auto& j=Joints[i]; j.Parent=s->ReadInt32();
		for (auto& child:j.Children) child=s->ReadInt32();
		j.Name=s->ReadName(); j.Group=s->ReadInt32(); j.Flags=s->ReadInt32(); j.BaseRotation=Rotation(s);
		for (auto& plane:j.CollisionPlanes) { vec3 xyz=Vector(s); plane=vec4(xyz,s->ReadFloat()); }
		if ((i==0 && j.Parent!=-1 && j.Parent!=0) || (i>0 && (j.Parent<0 || j.Parent>=(int)i)))
			Exception::Throw("Invalid Rune joint hierarchy");
	}
	Sequences.resize(Count(s, 16));
	for (auto& seq:Sequences)
	{
		auto& a=seq.Info; a.Name=s->ReadName(); a.Group=s->ReadName();
		a.StartFrame=s->ReadInt32(); a.NumFrames=s->ReadInt32();
		a.Notifys.resize(Count(s, 5));
		for (auto& n:a.Notifys) { n.Time=s->ReadFloat(); n.Function=s->ReadName(); }
		a.Rate=s->ReadFloat(); seq.Data=Bytes(s);
		if (a.NumFrames<=0 || a.StartFrame<0 || (int64_t)a.StartFrame+a.NumFrames>numFrames || !std::isfinite(a.Rate))
			Exception::Throw("Invalid Rune animation sequence");
		AnimSeqs.push_back(a);
	}
	Frames.resize(Count(s, 29));
	for (auto& f:Frames)
	{
		f.Sequence=s->ReadInt16(); f.Event=s->ReadName(); f.Bounds=Box(s);
		f.PreprocessJoints.resize(Count(s, 41));
		for (auto& p:f.PreprocessJoints) { p.Position=Vector(s); p.Rotation=Rotation(s); p.Scale=Vector(s); s->ReadFloat(); s->ReadUInt8(); }
		if (f.Sequence<0 || f.Sequence>=numSequences) Exception::Throw("Rune frame sequence out of range");
	}
	PositionOffset=Vector(s); RotationOffset=Rotation(s);
	SerializedEnd=s->Tell(); s->ThrowIfNotEnd();
	if (numMeshes!=(int)Meshes.size() || numJoints!=(int)Joints.size() || numFrames!=(int)Frames.size() || numSequences!=(int)Sequences.size())
		Exception::Throw("Rune model header/array mismatch");
	if (!Frames.empty()) { BoundingBox=Frames.front().Bounds; MeshBoundingBox=BoundingBox; }
	Origin=PositionOffset; RotOrigin=RotationOffset; Scale=vec3(1.0f);
	meshToObject=Coords::Rotation(RotOrigin).ToMatrix()*mat4::translate(-Origin);
}

void USkelModel::Save(PackageStreamWriter*)
{
	Exception::Throw("Saving Rune SkelModel resources is not implemented");
}

Array<RuneJointPose> USkelModel::DecodeFrame(size_t sequence, int frame, bool validateToEnd, UActor* actor) const
{
	if (sequence>=Sequences.size()) Exception::Throw("Rune sequence out of range");
	const auto& s=Sequences[sequence];
	if (frame<0 || frame>=s.Info.NumFrames) Exception::Throw("Rune frame out of range");
	Array<RuneJointPose> result; result.resize(Joints.size());
	size_t pos=0;
	auto byte=[&]() { if (pos>=s.Data.size()) Exception::Throw("Truncated Rune animation"); return s.Data[pos++]; };
	auto shortValue=[&]() { int lo=byte(), hi=byte(); return (int16_t)(lo|(hi<<8)); };
	int lastFrame=validateToEnd ? s.Info.NumFrames-1 : frame;
	for (int f=0;f<=lastFrame;f++) for (size_t joint=0;joint<Joints.size();joint++)
	{
		uint8_t bits=byte(); RuneJointPose p;
		if (bits&1) p.Position.x=shortValue()/256.0f;
		if (bits&2) p.Position.y=shortValue()/256.0f;
		if (bits&4) p.Position.z=shortValue()/256.0f;
		if (bits&8) { p.Scale.x=shortValue()/256.0f; p.Scale.z=shortValue()/256.0f; }
		if (bits&16) p.Scale.y=shortValue()/256.0f;
		if (bits&32) p.Rotation.Pitch=shortValue();
		if (bits&64) p.Rotation.Yaw=shortValue();
		if (bits&128) p.Rotation.Roll=shortValue();
		if (f==frame) result[joint]=p;
	}
	if (validateToEnd && pos!=s.Data.size()) Exception::Throw("Trailing bytes in Rune animation");
	if (actor)
	{
		// GetSkeleton selects group 0 from the actor and group 1 from AnimProxy.
		// Decode without an actor for the secondary pose to avoid recursion.
		auto* proxy=UObject::TryCast<UActor>(actor->GetUObject("AnimProxy"));
		if (proxy && !proxy->AnimSequence().IsNone())
		{
			for (size_t i=0;i<Sequences.size();i++) if (Sequences[i].Info.Name==proxy->AnimSequence())
			{
				int proxyFrame=(int)(std::max(0.0f,proxy->AnimFrame())*Sequences[i].Info.NumFrames)%Sequences[i].Info.NumFrames;
				auto upper=DecodeFrame(i,proxyFrame);
				for (size_t j=0;j<result.size();j++) if (Joints[j].Group==1) result[j]=upper[j];
				break;
			}
		}
		RuneDualWield::ApplyPose(*this,actor,result);
		ApplyDynamicPose(actor,result);
		for (size_t i=0;i<result.size() && i<50;i++) result[i].Rotation+=actor->RuneJointOffsets[i];
        RuneRushPose::Apply(*this,actor,result);
        RuneSlam::Apply(*this,actor,result);
        RuneCorpse::ApplyPose(*this,actor,result);
        RunePhysics::ApplyPose(actor,result);
	}
	return result;
}

Array<RuneJointPose> USkelModel::SampleFrame(size_t sequence,float phase,bool loop) const
{
    if(sequence>=Sequences.size()) Exception::Throw("Rune sequence out of range");
    const int count=Sequences[sequence].Info.NumFrames;
    if(!std::isfinite(phase)) phase=0;
    float f=std::max(0.0f,phase)*count;
    f=loop ? std::fmod(f,(float)count) : std::min(f,(float)(count-1));
    int first=(int)std::floor(f),second=loop?(first+1)%count:std::min(first+1,count-1);
    float alpha=f-first;
    auto pose=DecodeFrame(sequence,first);
    if(alpha>0 && second!=first) {
        auto next=DecodeFrame(sequence,second);
        for(size_t j=0;j<pose.size();j++) pose[j]=BlendRunePose(pose[j],next[j],alpha);
    }
    return pose;
}

Array<RuneJointPose> USkelModel::SampleAnimation(UActor* actor) const
{
    NameString name=actor->AnimSequence().IsNone()?actor->RuneResetSequence:actor->AnimSequence();
    size_t sequence=0;
    for(size_t i=0;i<Sequences.size();i++) if(Sequences[i].Info.Name==name){sequence=i;break;}
    auto pose=SampleFrame(sequence,actor->AnimFrame(),actor->bAnimLoop());
    if(actor->AnimFrame()<0 && actor->RuneTweenModel==this && actor->RuneTweenTarget==name && actor->RuneTweenPose.size()==pose.size()) {
        float t=std::clamp(1.0f+actor->AnimFrame()*Sequences[sequence].Info.NumFrames,0.0f,1.0f);
        for(size_t j=0;j<pose.size();j++) pose[j]=BlendRunePose(actor->RuneTweenPose[j],pose[j],t);
    }
    return pose;
}

void USkelModel::CaptureTween(UActor* actor) const
{
    // Sample before replacing the previous snapshot: nested transitions start
    // from the visible in-between pose, not a stale original keyframe.
    auto source=SampleAnimation(actor);
    actor->RuneTweenPose=std::move(source);
    actor->RuneTweenModel=this;
}

Array<RuneJointPose> USkelModel::ActorPose(UActor* actor) const
{
    auto pose=SampleAnimation(actor);
    auto* proxy=UObject::TryCast<UActor>(actor->GetUObject("AnimProxy"));
    if(proxy && !proxy->bDeleteMe() && !proxy->AnimSequence().IsNone() && GetRuneDisplayMesh(proxy)==this) {
        // Only overlay a sequence present in this skeleton. Actor and proxy
        // have independent phase, loop and transition clocks.
        bool valid=false;
        for(const auto& sequence:Sequences) if(sequence.Info.Name==proxy->AnimSequence()){valid=true;break;}
        if(valid) {
            auto upper=SampleAnimation(proxy);
            if(actor->NetworkReplica && proxy->RuneCombatPreviewModel==this &&
               proxy->RuneCombatPreviewWeight>0 && proxy->RuneCombatPreviewPose.size()==upper.size())
                for(size_t j=0;j<upper.size();++j)upper[j]=BlendRunePose(upper[j],proxy->RuneCombatPreviewPose[j],proxy->RuneCombatPreviewWeight);
            // A standing spin cannot combine hips from the body tween with
            // a torso from the proxy's different tween/attack phase.
            auto* wholeBody=RuneDualWield::FullBodyPoseSource(actor);
            if(wholeBody==proxy)pose=std::move(upper);
            else if(wholeBody!=actor)
                for(size_t j=0;j<pose.size();j++)if(Joints[j].Group==1)pose[j]=upper[j];
        }
    }
    RuneDualWield::ApplyPose(*this,actor,pose);
    ApplyDynamicPose(actor,pose);
    for(size_t j=0;j<pose.size() && j<50;j++) pose[j].Rotation+=actor->RuneJointOffsets[j];
    RuneRushPose::Apply(*this,actor,pose);
    RuneSlam::Apply(*this,actor,pose);
    RuneBash::ApplyPose(*this,actor,pose);
    RuneCorpse::ApplyPose(*this,actor,pose);
    RunePhysics::ApplyPose(actor,pose);
    return pose;
}

void USkelModel::ValidateAnimations() const
{
	for (size_t i=0;i<Sequences.size();i++) DecodeFrame(i,0,true);
}

Array<mat4> USkelModel::JointMatrices(const Array<RuneJointPose>& pose) const
{
	if (pose.size()!=Joints.size()) Exception::Throw("Rune pose/joint count mismatch");
	Array<mat4> result; result.resize(Joints.size());
	for (size_t i=0;i<Joints.size();i++)
	{
		mat4 rotation=RunePoseRotation(pose[i].Rotation);
		if (i==0) rotation=mat4::transpose(rotation);
		result[i]=mat4::translate(pose[i].Position)*rotation*mat4::scale(pose[i].Scale);
		if (i>0) result[i]=result[Joints[i].Parent]*result[i];
	}
	return result;
}

Array<vec3> USkelModel::SkinFrame(size_t mesh, size_t sequence, int frame, UActor* actor) const
{
	if (mesh>=Meshes.size()) Exception::Throw("Rune mesh variant out of range");
	auto bones=JointMatrices(DecodeFrame(sequence,frame,false,actor));
	Array<vec3> result; result.reserve(Meshes[mesh].Vertices.size());
	for (const auto& v:Meshes[mesh].Vertices)
	{
		vec3 p=(bones[v.Joint[0]]*vec4(v.Point[0],1.0f)).xyz()*v.Weight;
		if (v.Weight!=1) p+=(bones[v.Joint[1]]*vec4(v.Point[1],1.0f)).xyz()*(1-v.Weight);
		result.push_back(p);
	}
	return result;
}

void USkelModel::PrepareMesh(int variant, NameString sequence, float frame, UActor* actor)
{
    const auto* geometry=actor?GetRuneGeometry(actor):this;
    if(!geometry || geometry->Meshes.empty() || Sequences.empty())return;
    if(variant<0 || variant>=int(geometry->Meshes.size()))variant=0;
    const auto& m=geometry->Meshes[variant];
	size_t seq=0;
	for (size_t i=0;i<Sequences.size();i++) if (Sequences[i].Info.Name==sequence) { seq=i; break; }
	float f=std::max(0.0f,frame)*Sequences[seq].Info.NumFrames;
	int f0=(int)f%Sequences[seq].Info.NumFrames, f1=(f0+1)%Sequences[seq].Info.NumFrames;
	float alpha=f-std::floor(f);
    if(RuneAnimation::Interpolate) {
        auto bones=JointMatrices(actor?ActorPose(actor):SampleFrame(seq,frame,true));
        Verts.clear();Verts.reserve(m.Vertices.size());
        for(const auto& v:m.Vertices) {
            if(v.Joint[0]<0 || size_t(v.Joint[0])>=bones.size() || (v.Weight!=1 && (v.Joint[1]<0 || size_t(v.Joint[1])>=bones.size())))
                Exception::Throw("Rune substitute mesh has incompatible joints");
            vec3 point=(bones[v.Joint[0]]*vec4(v.Point[0],1.0f)).xyz()*v.Weight;
            if(v.Weight!=1) point+=(bones[v.Joint[1]]*vec4(v.Point[1],1.0f)).xyz()*(1-v.Weight);
            Verts.push_back(point);
        }
    } else {
        const auto bones=JointMatrices(DecodeFrame(seq,f0,false,actor));
        const auto next=JointMatrices(DecodeFrame(seq,f1,false,actor));
        Verts.clear();Verts.reserve(m.Vertices.size());
        for(const auto& v:m.Vertices) {
            if(v.Joint[0]<0 || size_t(v.Joint[0])>=bones.size() || (v.Weight!=1 && (v.Joint[1]<0 || size_t(v.Joint[1])>=bones.size())))
                Exception::Throw("Rune substitute mesh has incompatible joints");
            auto skin=[&](const Array<mat4>& pose){auto point=(pose[v.Joint[0]]*vec4(v.Point[0],1)).xyz()*v.Weight;
                if(v.Weight!=1)point+=(pose[v.Joint[1]]*vec4(v.Point[1],1)).xyz()*(1-v.Weight);return point;};
            Verts.push_back(mix(skin(bones),skin(next),alpha));
        }
    }
	FrameVerts=(int)Verts.size(); AnimFrames=1;
	Tris.clear(); Normals.assign(Verts.size(),vec3(0.0f));
	for (const auto& t:m.Triangles)
	{
		MeshTri tri={}; tri.TextureIndex=t.Group; tri.PolyFlags=m.GroupFlags[t.Group];
		for (int i=0;i<3;i++) { tri.Indices[i]=t.Index[i]; tri.UV[i]=t.UV[i]; }
		Tris.push_back(tri);
		vec3 n=cross(Verts[t.Index[1]]-Verts[t.Index[0]],Verts[t.Index[2]]-Verts[t.Index[0]]);
		for (auto v:t.Index) Normals[v]+=n;
	}
	for (auto& n:Normals) { float len=length(n); n=len>0 ? n/len : vec3(0,0,1); }
	Textures.resize(16);
	for (int i=0;i<16;i++) Textures[i]=m.SkinNames[i].IsNone() ? nullptr : UObject::TryCast<UTexture>(geometry->package->GetUObject("Texture",m.SkinNames[i]));
}
