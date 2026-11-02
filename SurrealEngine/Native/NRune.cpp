#include "Precomp.h"
#include "RuneDualWield.h"
#include "NRune.h"
#include "VM/NativeFunc.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Math/coords.h"
#include <cmath>
#include <algorithm>
#include "Engine.h"
#include "Render/RenderSubsystem.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "VM/Iterator.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <limits>

vec3 NRune::ArcVelocity(int trajectory, const vec3& source, const vec3& destination, float gravity)
{
	// execCalcArcVelocity: fixed launch pitch, destination yaw, zone gravity
	// magnitude. Solve z = r*tan(pitch) - g*r*r/(2*v*v*cos(pitch)^2).
	vec3 delta = destination - source;
	double distance = std::hypot(delta.x, delta.y);
	double pitch = (trajectory & 65535) * (3.14159265358979323846 / 32768.0);
	double c = std::cos(pitch), s = std::sin(pitch);
	double denominator = 2.0 * c * (distance * s - delta.z * c);
	if (distance <= 0.0 || denominator <= 0.0 || std::abs(c) < 1e-8 || !std::isfinite(gravity) || gravity <= 0.0f)
		return vec3(0.0f);
	double speed = std::sqrt(distance * distance * gravity / denominator);
	vec3 velocity((float)(delta.x / distance * speed * c), (float)(delta.y / distance * speed * c), (float)(speed * s));
	return std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z) ? velocity : vec3(0.0f);
}

bool NRune::ClipJointBox(const vec4* planes,const vec3& start,const vec3& end,float radius,float& fraction,vec3& normal)
{
	float enter=0.0f,leave=1.0f;
	bool inside=true;
	normal=vec3(0.0f);
	for(int i=0;i<6;i++)
	{
		vec3 n=planes[i].xyz();
		float support=radius*(std::abs(n.x)+std::abs(n.y)+std::abs(n.z));
		float a=dot(n,start)-planes[i].w-support;
		float b=dot(n,end)-planes[i].w-support;
		if(a>0.0f) inside=false;
		if(a>0.0f && b>0.0f) return false;
		if(a<=0.0f && b<=0.0f) continue;
		float t=a/(a-b);
		if(a>b){if(t>enter){enter=t;normal=n;}}
		else leave=std::min(leave,t);
		if(enter>leave) return false;
	}
	float distance=length(end-start);
	if(inside){fraction=0.0f;normal=distance>0?(end-start)/distance:vec3(0,0,1);return true;}
	if(enter>=1.0f) return false;
	// LineCheckJointBox backs the hit away from the plane by 0.1 world units.
	fraction=std::max(0.0f,enter-std::min(0.1f,distance>0?0.1f/distance:0.1f));
	return true;
}

namespace
{
void NearestNavPoint(UObject* self, UObject*& result)
{
	result = nullptr;
	auto* pawn = UObject::Cast<UPawn>(self);
	if (!pawn->Level()) return;
	// execNearestNavPoint compares only squared 3D distance, with this exact
	// initial bound (Engine.dll 0x104441d0). It does not test reachability.
	float bestDistanceSquared = 9999999.0f;
	for (auto* nav = pawn->Level()->NavigationPointList(); nav; nav = nav->nextNavigationPoint())
	{
		vec3 delta = nav->Location() - pawn->Location();
		float distanceSquared = dot(delta, delta);
		if (distanceSquared < bestDistanceSquared)
		{
			bestDistanceSquared = distanceSquared;
			result = nav;
		}
	}
}

void StopAllSound(UObject*)
{
	// Original ALAudio StopAllSound only clears effect channels. StopSounds is
	// viewport teardown in this engine and would also stop music/disconnect audio.
	if (engine->audiodev) engine->audiodev->StopAllEffects();
}

void PolyobjGetCollisionRadius(UObject* self, float& result)
{
	auto* brush = UObject::Cast<UBrush>(self);
	if (!brush->Brush()) Exception::Throw("Polyobj.GetCollisionRadius requires brush geometry");
	// Original native668 requests the primitive's render bounds with precise=0,
	// then returns half its diagonal. UModel transforms its cached local AABB.
	result = length(brush->Brush()->BoundingBox.transform(GetMovingBrushTransform(brush)).extents());
}

void PolyobjGetTexture(UObject* self, UObject*& result)
{
	result = nullptr;
	auto* actor = UObject::Cast<UActor>(self);
	if (!actor->Brush() || !actor->XLevel() || !actor->XLevel()->Model) return;
	auto* world = actor->XLevel()->Model;
	Array<UTexture*> textures;
	std::map<UTexture*, size_t> counts;
	for (const auto& node : actor->Brush()->Nodes)
	{
		if (node.Surf < 0 || (size_t)node.Surf >= world->Surfaces.size()) continue;
		const auto& surface = world->Surfaces[node.Surf];
		if (surface.PolyFlags & PF_Invisible) continue;
		auto* texture = surface.Material;
		if (texture)
		{
			if (engine->render) engine->render->UpdateTexture(texture);
			texture = texture->GetAnimTexture();
		}
		textures.push_back(texture);
		counts[texture]++;
	}
	// Preserve first-surface preference on a final-count tie. Incremental winner
	// selection would incorrectly prefer whichever texture reached its count first.
	size_t bestCount = 0;
	for (auto* texture : textures)
	{
		if (counts[texture] > bestCount)
		{
			result = texture;
			bestCount = counts[texture];
		}
	}
}

void WaitForRelease(UObject* self)
{
	// The original uses the same latent poller as Sleep, with a 9999-second
	// countdown. Release is not a queued signal and can also wake Sleep.
	auto* actor = UObject::Cast<UActor>(self);
	actor->LatentFloat() = 9999.0f;
	if (self->StateFrame) self->StateFrame->SetLatentState(LatentRunState::Sleep);
}

void Release(UObject* self)
{
	if (self->StateFrame && self->StateFrame->GetLatentState() == LatentRunState::Sleep)
		UObject::Cast<UActor>(self)->LatentFloat() = -1.0f;
}

void FindWaterLine(UObject* self, const vec3& start, const vec3& end, vec3& result)
{
	auto* actor = UObject::Cast<UActor>(self);
	result = end;
	auto finite = [](const vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
	if (!finite(start) || !finite(end) || !actor->XLevel() || !actor->XLevel()->Model || !actor->Region().Zone)
		return;
	const bool water = actor->Region().Zone->bWaterZone();
	vec3 outside = start;
	// Original APawn::findWaterLine bisects against the body's current zone
	// water flag until the endpoints are less than one world unit apart. It
	// does not check for an endpoint crossing: matching midpoints replace End.
	for (int iteration = 0; iteration < 256 && dot(result - outside, result - outside) >= 1.0f; iteration++)
	{
		vec3 middle = outside * 0.5f + result * 0.5f;
		if (middle == outside || middle == result) break; // Float precision guard.
		auto* zone = actor->XLevel()->Model->FindRegion(middle, actor->Level()).Zone;
		if (!zone) break;
		if (static_cast<bool>(zone->bWaterZone()) == water) result = middle;
		else outside = middle;
	}
}

void CalcArcVelocity(UObject* self, int trajectory, const vec3& source, const vec3& destination, vec3& result)
{
	auto* actor=UObject::Cast<UActor>(self);
	auto* zone=actor->Region().Zone;
	result=NRune::ArcVelocity(trajectory,source,destination,zone ? length(zone->ZoneGravity()) : 0.0f);
}

void AnnounceOfflineStats()
{
	if (!engine->RuneStatsAnnounced)
	{
		engine->RuneStatsAnnounced=true;
		LogMessage("RUNE_STATS backend=offline-session persistence=none steam_sync=0");
	}
}
void ModifyIntStat(UObject*, uint8_t stat, int delta)
{
	AnnounceOfflineStats();
	if (stat>0 && stat<44)
		engine->RuneIntStats[stat]=(int)std::clamp<int64_t>((int64_t)engine->RuneIntStats[stat]+delta,std::numeric_limits<int>::min(),std::numeric_limits<int>::max());
}
void ModifyFloatStat(UObject*, uint8_t stat, float delta)
{
	AnnounceOfflineStats();
	if (stat>0 && stat<44 && std::isfinite(delta) && std::isfinite(engine->RuneFloatStats[stat]+delta)) engine->RuneFloatStats[stat]+=delta;
}
void GetIntStat(UObject*, uint8_t stat, int& result) { result=stat>0 && stat<44 ? engine->RuneIntStats[stat] : 0; }
void GetFloatStat(UObject*, uint8_t stat, float& result) { result=stat>0 && stat<44 ? engine->RuneFloatStats[stat] : 0.0f; }
void UnlockAchievement(UObject*, uint8_t achievement)
{
	AnnounceOfflineStats();
	if (achievement<29) engine->RuneAchievements[achievement]=true;
}
void GetAchievement(UObject*, uint8_t achievement, BitfieldBool& result) { result=achievement<29 && engine->RuneAchievements[achievement]; }

struct RuneSweepHit
{
	UActor* Actor;
	vec3 Location,Normal;
	float Fraction;
	uint32_t Low=0,High=0;
};
struct RuneJointHull
{
	vec4 Planes[6];
	int Joint;
};
struct RuneSweepTarget
{
	UActor* Actor;
	Array<RuneJointHull> Hulls;
};
class RuneSweepIterator : public Iterator
{
public:
	RuneSweepIterator(UActor* caller,UObject* baseClass,UObject** output,const vec3& b1,const vec3& e1,const vec3& b2,const vec3& e2,float radius,vec3& location,vec3& normal,int& low,int& high)
		: Caller(caller),Output(output),Location(location),Normal(normal),Low(low),High(high)
	{
		auto* level=caller->XLevel();
		if(!level || !std::isfinite(radius)) return;
		radius=std::max(0.001f,radius);
		auto eligible=[&](UActor* actor)
		{
			return actor && actor!=caller && actor!=caller->Instigator() && actor!=caller->Owner() && !actor->bDeleteMe() &&
				actor->GetBool("bSweepable") && (!baseClass || actor->IsA(baseClass->Name));
		};
		Array<RuneSweepTarget> targets;
		for(auto* actor:level->Actors)
		{
			if(!eligible(actor) || actor->DrawScale()==0) continue;
			auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));
			if(!model) continue;
			auto* flags=static_cast<uint8_t*>(actor->GetProperty("JointFlags"));
			RuneSweepTarget target;target.Actor=actor;
			auto bones=model->ActorJointMatrices(actor);
			mat4 world=GetRuneMeshToWorld(actor);
			for(size_t j=0;j<model->Joints.size() && j<50;j++) if(flags[j]&0x10)
			{
				RuneJointHull hull;hull.Joint=(int)j;
				mat4 transform=world*bones[j];
				mat3 normalTransform=mat3::transpose(mat3::inverse(mat3(transform)));
				vec3 origin=(transform*vec4(0,0,0,1)).xyz();
				for(int p=0;p<6;p++)
				{
					vec4 plane=model->Joints[j].CollisionPlanes[p];
					vec3 worldNormal=normalTransform*plane.xyz();
					plane=vec4(worldNormal,plane.w+dot(worldNormal,origin));
					float n=length(plane.xyz());
					if(n<=0 || !std::isfinite(n)) Exception::Throw("Invalid Rune joint collision plane");
					hull.Planes[p]=vec4(plane.xyz()/n,plane.w/n);
				}
				target.Hulls.push_back(hull);
			}
			if(!target.Hulls.empty()) targets.push_back(std::move(target));
		}
		float travel=std::max(length(b2-b1),length(e2-e1));
		if(!std::isfinite(travel)) return;
		int steps=(int)std::min(100.0f,travel/radius+1.0f);
		// Sample both endpoints; the original iterator also subdivides by radius
		// and caps at 100. Exact sampling/edge-case parity is still unverified.
		for(int step=0;step<=steps;step++)
		{
			float alpha=(float)step/steps;
			vec3 start=mix(b1,b2,alpha),end=mix(e1,e2,alpha);
			Array<RuneSweepHit> slice;
			float worldLimit=1.0f;
			for(const auto& hit:level->Collision.Trace(start,end,radius,radius,false,true,false))
			{
				worldLimit=std::min(worldLimit,hit.Fraction);
				if(!baseClass || hit.Actor->IsA(baseClass->Name)) slice.push_back({hit.Actor,mix(start,end,hit.Fraction),hit.Normal,hit.Fraction});
				break;
			}
			for(const auto& target:targets)
			{
				RuneSweepHit nearest={target.Actor,{}, {},worldLimit};
				bool found=false;
				for(const auto& hull:target.Hulls)
				{
					float fraction;vec3 normal;
					if(NRune::ClipJointBox(hull.Planes,start,end,radius,fraction,normal) && fraction<nearest.Fraction)
					{
						nearest={target.Actor,mix(start,end,fraction),normal,fraction};
						if(hull.Joint<32) nearest.Low=uint32_t(1)<<hull.Joint;
						else nearest.High=uint32_t(1)<<(hull.Joint-32);
						found=true;
					}
				}
				if(found) slice.push_back(nearest);
			}
			for(const auto& hit:level->Collision.Trace(start,end,radius,radius,true,false,false))
			{
				if(!eligible(hit.Actor) || hit.Fraction>worldLimit) continue;
				bool skeletal=false;
				for(const auto& target:targets) if(target.Actor==hit.Actor){skeletal=true;break;}
				if(!skeletal) slice.push_back({hit.Actor,mix(start,end,hit.Fraction),hit.Normal,hit.Fraction});
			}
			std::stable_sort(slice.begin(),slice.end(),[](const auto& a,const auto& b){return a.Fraction<b.Fraction;});
			for(size_t i=0;i<slice.size() && i<64;i++) Hits.push_back(slice[i]);
		}
	}
	bool Next() override
	{
		while(Index<Hits.size())
		{
			const auto& hit=Hits[Index++];
			if(hit.Actor->bDeleteMe()) continue;
			if(commandline->HasArg("","--trace-rune-combat")) LogMessage("RUNE_SWEEP_HIT actor="+hit.Actor->Name.ToString()+" low="+std::to_string(hit.Low)+" high="+std::to_string(hit.High)+" caller="+Caller->Name.ToString());
			*Output=hit.Actor;Location=hit.Location;Normal=hit.Normal;Low=(int)hit.Low;High=(int)hit.High;return true;
		}
		*Output=nullptr;Low=0;High=0;return false;
	}
private:
	UActor* Caller;
	UObject** Output;
	vec3& Location;
	vec3& Normal;
	int& Low;
	int& High;
	Array<RuneSweepHit> Hits;
	size_t Index=0;
};
void SweepActors(UObject* self,UObject* baseClass,UObject*& actor,const vec3& b1,const vec3& e1,const vec3& b2,const vec3& e2,float radius,vec3& location,vec3& normal,int& low,int& high)
{
	Frame::CreatedIterator=std::make_unique<RuneSweepIterator>(UObject::Cast<UActor>(self),baseClass,&actor,b1,e1,b2,e2,radius,location,normal,low,high);
}
USkelModel* Model(UObject* self)
{
	return UObject::TryCast<USkelModel>(GetRuneDisplayMesh(UObject::Cast<UActor>(self)));
}
void NumJoints(UObject* self, int& result)
{
	auto* model=Model(self); result=model ? (int)model->Joints.size() : 0;
}
void GetJointName(UObject* self, int joint, std::string& result)
{
	auto* model=Model(self);
	result=(model && joint>=0 && joint<(int)model->Joints.size()) ? model->Joints[joint].Name.ToString() : "";
}
void JointNamed(UObject* self, const NameString& name, int& result)
{
	result=-1;
	if (auto* model=Model(self)) for (size_t i=0;i<model->Joints.size();i++)
		if (model->Joints[i].Name==name) { result=(int)i; break; }
}
void SetDefaultPolygroups(UObject* self)
{
	auto* actor=UObject::Cast<UActor>(self); auto* model=GetRuneGeometry(actor);
	if (!model || model->Meshes.empty()) return;
	int variant=actor->GetByte("SkelMesh");
	if (variant>=(int)model->Meshes.size()) Exception::Throw("Rune SkelMesh out of range");
	auto* flags=static_cast<int*>(actor->GetProperty("SkelGroupFlags"));
	for (int i=0;i<16;i++) flags[i]=model->Meshes[variant].GroupFlags[i];
}
void SetDefaultJointFlags(UObject* self)
{
	auto* actor=UObject::Cast<UActor>(self); auto* model=Model(self);
	auto* flags=static_cast<uint8_t*>(actor->GetProperty("JointFlags"));
	for (size_t i=0;i<50;i++) flags[i]=(model && i<model->Joints.size()) ? (uint8_t)model->Joints[i].Flags : 0;
	actor->RuneDynamics.clear(); actor->RuneDynamicModel=nullptr;
    actor->RuneTweenPose.clear(); actor->RuneTweenModel=nullptr;
}
bool ValidJoint(USkelModel* model, int joint)
{
	return model && joint>=0 && joint<(int)model->Joints.size() && joint<50;
}
void ResetAnimationCache(UObject* self, const NameString& sequence)
{
	auto* actor=UObject::Cast<UActor>(self); auto* model=Model(self);
	if (!model) return;
	for (size_t i=0;i<model->Sequences.size();i++) if (model->Sequences[i].Info.Name==sequence)
	{
		model->DecodeFrame(i,0);
		actor->RuneResetSequence=sequence;
		actor->RuneDynamics.clear(); actor->RuneDynamicModel=nullptr;
    actor->RuneTweenPose.clear(); actor->RuneTweenModel=nullptr;
		return;
	}
}
void AttachActorToJoint(UObject* self, UObject* childObject, int joint)
{
	auto* parent=UObject::Cast<UActor>(self); auto* child=UObject::Cast<UActor>(childObject);
	if (!child || !ValidJoint(Model(self),joint)) return;
	// Joint attachment replaces any floor/carrier link inherited at spawn.
	// Otherwise a thrown item can remain in the old base's BasedActors list.
	child->SetBase(nullptr, true);
	auto* children=static_cast<UObject**>(parent->GetProperty("JointChild"));
	children[joint]=child;
	child->SetBool("bCarriedItem",true);
	child->SetObject("AttachParent",parent);
	child->SetByte("AttachParentJoint",(uint8_t)joint);
}
void ActorAttachedTo(UObject* self, int joint, UObject*& result)
{
	result=nullptr;
	if (ValidJoint(Model(self),joint)) result=static_cast<UObject**>(self->GetProperty("JointChild"))[joint];
}
void DetachActorFromJoint(UObject* self,int joint,UObject*& result)
{
	result=nullptr;
	if(!ValidJoint(Model(self),joint)) return;
	auto* children=static_cast<UObject**>(self->GetProperty("JointChild"));
	auto* child=UObject::TryCast<UActor>(children[joint]);
	children[joint]=nullptr;
	if(!child) return;
	mat4 world=GetRuneActorToWorld(child);
	vec3 position=(world*vec4(0,0,0,1)).xyz();
	vec3 x=normalize((world*vec4(1,0,0,0)).xyz());
	vec3 y=normalize((world*vec4(0,1,0,0)).xyz());
	Rotator rotation=Rotator::FromVector(x);
	Coords axes=Coords::Rotation(rotation);
	rotation.Roll=(int)(std::atan2(-dot(y,axes.ZAxis),dot(y,axes.YAxis))*65536.0f/(2.0f*3.14159265358979323846f));
	child->SetBool("bCarriedItem",false);
	child->SetObject("AttachParent",nullptr);
	child->SetByte("AttachParentJoint",0);
	// Also clear legacy links restored from saves made before attachment cleanup.
	child->SetBase(nullptr, true);
	if(auto* level=child->XLevel()) level->Collision.RemoveFromCollision(child);
	child->Location()=position;child->Rotation()=rotation;
	if(auto* level=child->XLevel()) level->Collision.AddToCollision(child);
	result=child;
}
void GetJointPos(UObject* self, int joint, vec3& result)
{
	auto* actor=UObject::Cast<UActor>(self); auto* model=Model(self);
	result=actor->Location();
	if (!ValidJoint(model,joint)) return;
	NameString sequence=actor->AnimSequence().IsNone() ? actor->RuneResetSequence : actor->AnimSequence();
	size_t seq=0;
	for (size_t i=0;i<model->Sequences.size();i++) if (model->Sequences[i].Info.Name==sequence) { seq=i; break; }
	int frame=(int)(std::max(0.0f,actor->AnimFrame())*model->Sequences[seq].Info.NumFrames)%model->Sequences[seq].Info.NumFrames;
	auto bones=RuneAnimation::Interpolate?model->ActorJointMatrices(actor):model->JointMatrices(model->DecodeFrame(seq,frame,false,actor));
	mat4 toWorld=GetRuneMeshToWorld(actor);
	result=(toWorld*bones[joint]*vec4(0,0,0,1)).xyz();
}
void GetJointRot(UObject* self, int joint, Rotator& result)
{
	auto* actor=UObject::Cast<UActor>(self); auto* model=Model(self);
	result={0,0,0};
	if (!ValidJoint(model,joint)) return;
	// execGetJointRot takes the latest joint coordinates, removes the model's
	// RotationOffset and extracts their world orientation (native 603).
	mat4 transform=GetRuneMeshToWorld(actor)*model->ActorJointMatrices(actor)[joint]*
		Coords::InverseRotation(model->RotationOffset).ToMatrix();
	vec3 x=(transform*vec4(1,0,0,0)).xyz(), y=(transform*vec4(0,1,0,0)).xyz();
	if (length(x)<1e-8f || length(y)<1e-8f) return;
	x=normalize(x); y=normalize(y);
	result=Rotator::FromVector(x);
	Coords axes=Coords::Rotation(result);
	result.Roll=(int)(std::atan2(-dot(y,axes.ZAxis),dot(y,axes.YAxis))*65536.0f/(2.0f*3.14159265358979323846f));
}
void ApplyJointForce(UObject* self, int joint, const vec3& force)
{
	if(auto* model=Model(self)) model->ApplyJointForce(UObject::Cast<UActor>(self),joint,force);
}
// Native cache +0x94 stores Rune's model-axis rotation delta. These swizzles
// follow execSetJointRot and USkelModel::TurnJointTo from the shipped engine.
void SetJointRot(UObject* self, int joint, const Rotator& rotation)
{
	auto* actor=UObject::Cast<UActor>(self);
	if (ValidJoint(Model(self),joint)) actor->RuneJointOffsets[joint]={-rotation.Yaw,-rotation.Roll,-rotation.Pitch};
}
void TurnJointTo(UObject* self, int joint, const Rotator& rotation)
{
	auto* actor=UObject::Cast<UActor>(self);
	if (ValidJoint(Model(self),joint))
	{
		Rotator delta=rotation-actor->Rotation();
		actor->RuneJointOffsets[joint]={delta.Yaw,delta.Roll,delta.Pitch};
	}
}
void SkeletonLook(UObject* self, float seconds)
{
	// Recovered Pawn.Look script corroborated against execSkeletonLook.
	auto* actor=UObject::Cast<UActor>(self);
	if (!actor->GetBool("bCanLook") || (int)actor->GetInt("Health")<=0) return;
	Rotator root=actor->Rotation(); root.Pitch=0;
	Rotator target=actor->GetRotator("targetangle");
	if (!actor->GetBool("bOverrideLookTarget"))
	{
		auto* look=UObject::TryCast<UActor>(actor->GetUObject("LookTarget"));
		vec3 spot=actor->GetVector("LookSpot");
		if (look) target=Rotator::FromVector(look->Location()-actor->Location())-root;
		else if (spot!=vec3(0.0f)) target=Rotator::FromVector(spot-actor->Location())-root;
		else target={0,0,0};
	}
	auto wrap=[](int n) { while(n>32768)n-=65535; while(n<-32768)n+=65535; return n; };
	target.Yaw=wrap(target.Yaw); target.Pitch=wrap(target.Pitch);
	float rate=actor->GetFloat("LookDegPerSec")*65535.0f/360.0f;
	if (std::cos(target.Yaw*2.0f*3.14159265358979323846f/65535.0f)<actor->GetFloat("PeripheralVision")) { target={0,0,0}; rate*=0.5f; }
	Rotator look=actor->GetRotator("LookAngle");
	auto approach=[](int value,int goal,float step) { return value>goal ? std::max(goal,(int)(value-step)) : std::min(goal,(int)(value+step)); };
	look.Yaw=approach(look.Yaw,target.Yaw,rate*seconds);
	look.Pitch=approach(look.Pitch,target.Pitch,0.5f*actor->GetFloat("LookDegPerSec")*65535.0f/360.0f*seconds);
	actor->SetRotator("targetangle",target); actor->SetRotator("LookAngle",look);
	Rotator maxHead=actor->GetRotator("MaxHeadAngle"),maxBody=actor->GetRotator("MaxBodyAngle");
	int headUp=maxHead.Pitch*(actor->GetBool("bHeadLookUpDouble") ? 2 : 1);
	Rotator head={0,0,0},body={0,0,0};
	if (actor->GetBool("bRotateHead"))
	{
		head=look;
		if(head.Yaw>maxHead.Yaw){body.Yaw=head.Yaw-maxHead.Yaw;head.Yaw=maxHead.Yaw;}
		else if(head.Yaw<-maxHead.Yaw){body.Yaw=head.Yaw+maxHead.Yaw;head.Yaw=-maxHead.Yaw;}
		if(head.Pitch>headUp){body.Pitch=head.Pitch-headUp;head.Pitch=headUp;}
		else if(head.Pitch<-maxHead.Pitch){body.Pitch=head.Pitch+maxHead.Pitch;head.Pitch=-maxHead.Pitch;}
	}
	else if (actor->GetBool("bRotateTorso")) body=look;
	if(maxHead.Roll>0)head.Roll=head.Yaw/2;
	if(maxBody.Roll>0)body.Roll=body.Yaw/2;
	body={std::clamp(body.Pitch,-maxBody.Pitch,maxBody.Pitch),std::clamp(body.Yaw,-maxBody.Yaw,maxBody.Yaw),std::clamp(body.Roll,-maxBody.Roll,maxBody.Roll)};
	head={std::clamp(head.Pitch,-maxHead.Pitch,headUp),std::clamp(head.Yaw,-maxHead.Yaw,maxHead.Yaw),std::clamp(head.Roll,-maxHead.Roll,maxHead.Roll)};
	int joint=-1; JointNamed(actor,"torso",joint);
	if(joint>0)TurnJointTo(actor,joint,actor->Rotation()+(actor->GetBool("bRotateTorso")?body:Rotator(0,0,0)));
	JointNamed(actor,"head",joint);
	if(joint>0)TurnJointTo(actor,joint,actor->Rotation()+(actor->GetBool("bRotateHead")?head:Rotator(0,0,0)));
}
void DrawLine(UObject*, float x1, float y1, float x2, float y2, float r, float g, float b)
{
	engine->render->Draw2DLine(vec4(r/255.0f,g/255.0f,b/255.0f,1),0,vec3(x1,y1,0),vec3(x2,y2,0),true);
}
void DrawLine3D(UObject*, const vec3& p1, const vec3& p2, float r, float g, float b)
{
	engine->render->Draw3DLine(vec4(r/255.0f,g/255.0f,b/255.0f,1),0,p1,p2);
}
void TraceTexture(UObject* self, const vec3& end, const vec3& start, int& flags, vec3& scroll, UObject*& result)
{
	// Rune's native 666 is a world-surface query, unlike UT's iterator 1000.
	// Engine.dll 103d6420 returns surface flags and zone-scaled U/V axes.
	result=nullptr;
	flags=0;
	scroll=vec3(0.0f);
	auto* actor=UObject::Cast<UActor>(self);
	auto* level=actor->XLevel();
	if (!level || !level->Model || level->Model->Nodes.empty()) return;
	vec3 delta=end-start;
	float distance=length(delta);
	if (distance<=0.000001f) return;
	auto hits=level->Collision.TraceDecal(to_dvec3(start),0.0,to_dvec3(delta/distance),distance,false);
	if (hits.empty() || !hits.front().Node) return;
	const auto& surface=level->Model->Surfaces[hits.front().Node->Surf];
	result=surface.Material;
	flags=(int)surface.PolyFlags;
	auto* zone=hits.front().Actor ? hits.front().Actor->Region().Zone : nullptr;
	if (zone)
	{
		if (surface.PolyFlags & PF_AutoUPan) scroll+=level->Model->Vectors[surface.vTextureU]*zone->TexUPanSpeed();
		if (surface.PolyFlags & PF_AutoVPan) scroll+=level->Model->Vectors[surface.vTextureV]*zone->TexVPanSpeed();
	}
}
void FrameSweep(UObject* self, int frame, const vec3& weaponVector, vec3& lastBegin, vec3& lastEnd)
{
	auto* weapon=UObject::Cast<UActor>(self);
	auto* parent=UObject::TryCast<UActor>(weapon->GetUObject("AttachParent"));
	auto* model=parent?Model(parent):nullptr;
	int joint=weapon->GetByte("AttachParentJoint");
	if (!ValidJoint(model,joint)) return;
	auto* proxy=UObject::TryCast<UActor>(parent->GetUObject("AnimProxy"));
	NameString name=proxy?proxy->AnimSequence():parent->AnimSequence();
	for (size_t sequence=0;sequence<model->Sequences.size();sequence++) if (model->Sequences[sequence].Info.Name==name)
	{
		if (frame<0 || frame>=model->Sequences[sequence].Info.NumFrames) return;
		// FrameSweep samples the entire parent skeleton at this exact frame of
		// the attack, independently of the current render interpolation.
		auto pose=model->DecodeFrame(sequence,frame);
        RuneDualWield::ApplyPose(*model,parent,pose);
		for (size_t i=0;i<pose.size() && i<50;i++) pose[i].Rotation+=parent->RuneJointOffsets[i];
		auto bones=model->JointMatrices(pose);
		mat4 transform=GetRuneMeshToWorld(parent)*RuneDualWield::Attachment(*model,parent,weapon,bones);
		vec3 begin=(transform*vec4(0,0,0,1)).xyz();
		vec3 end=(transform*vec4(weaponVector,1)).xyz();
		CallEvent(weapon,"FrameSwept",{ExpressionValue::VectorValue(lastBegin),ExpressionValue::VectorValue(lastEnd),ExpressionValue::VectorValue(begin),ExpressionValue::VectorValue(end)});
		lastBegin=begin;
		lastEnd=end;
		return;
	}
}
}

void NRune::RegisterFunctions()
{
	RegisterVMNativeFunc_0("Actor", "StopAllSound", &StopAllSound, 268);
	RegisterVMNativeFunc_1("Polyobj", "GetCollisionRadius", &PolyobjGetCollisionRadius, 668);
	RegisterVMNativeFunc_1("Polyobj", "GetTexture", &PolyobjGetTexture, 669);
	RegisterVMNativeFunc_0("Actor", "WaitForRelease", &WaitForRelease, 660);
	RegisterVMNativeFunc_0("Actor", "Release", &Release, 661);
	// Rune stores its shared Sleep/WaitForRelease poller as 384. Keep the
	// previously registered 257 read alias for older prototype saves.
	RegisterLatentAction(384, LatentRunState::Sleep);
	RegisterVMNativeFunc_3("Pawn", "FindWaterLine", &FindWaterLine, 535);
	RegisterVMNativeFunc_1("Pawn", "NearestNavPoint", &NearestNavPoint, 664);
	RegisterVMNativeFunc_4("Actor", "CalcArcVelocity", &CalcArcVelocity, 663);
	RegisterVMNativeFunc_2("PlayerPawn", "ModifyIntStat", &ModifyIntStat, 0);
	RegisterVMNativeFunc_2("PlayerPawn", "ModifyFloatStat", &ModifyFloatStat, 0);
	RegisterVMNativeFunc_2("PlayerPawn", "GetIntStat", &GetIntStat, 0);
	RegisterVMNativeFunc_2("PlayerPawn", "GetFloatStat", &GetFloatStat, 0);
	RegisterVMNativeFunc_1("PlayerPawn", "UnlockAchievement", &UnlockAchievement, 0);
	RegisterVMNativeFunc_2("PlayerPawn", "GetAchievement", &GetAchievement, 0);
	RegisterVMNativeFunc_2("Actor", "GetJointName", &GetJointName, 605);
	RegisterVMNativeFunc_1("Actor", "NumJoints", &NumJoints, 609);
	RegisterVMNativeFunc_0("Actor", "SetDefaultPolygroups", &SetDefaultPolygroups, 610);
	RegisterVMNativeFunc_0("Actor", "SetDefaultJointFlags", &SetDefaultJointFlags, 611);
	RegisterVMNativeFunc_2("Actor", "JointNamed", &JointNamed, 619);
	RegisterVMNativeFunc_2("Actor", "GetJointPos", &GetJointPos, 602);
	RegisterVMNativeFunc_2("Actor", "GetJointRot", &GetJointRot, 603);
	RegisterVMNativeFunc_2("Actor", "ApplyJointForce", &ApplyJointForce, 608);
	RegisterVMNativeFunc_2("Actor", "AttachActorToJoint", &AttachActorToJoint, 613);
	RegisterVMNativeFunc_2("Actor", "ActorAttachedTo", &ActorAttachedTo, 617);
	RegisterVMNativeFunc_2("Actor", "DetachActorFromJoint", &DetachActorFromJoint, 614);
	RegisterVMNativeFunc_1("Actor", "ResetAnimationCache", &ResetAnimationCache, 620);
	RegisterVMNativeFunc_2("Actor", "SetJointRot", &SetJointRot, 621);
	RegisterVMNativeFunc_2("Actor", "TurnJointTo", &TurnJointTo, 616);
	RegisterVMNativeFunc_1("Pawn", "SkeletonLook", &SkeletonLook, 670);
	RegisterVMNativeFunc_7("Canvas", "DrawLine", &DrawLine, 490);
	RegisterVMNativeFunc_5("Canvas", "DrawLine3D", &DrawLine3D, 491);
	RegisterVMNativeFunc_5("Actor", "TraceTexture", &TraceTexture, 666);
	RegisterVMNativeFunc_4("Actor", "FrameSweep", &FrameSweep, 622);
	RegisterVMNativeFunc_11("Actor", "SweepActors", &SweepActors, 313);
}
