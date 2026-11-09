#pragma once

#include "UMesh.h"
#include "RunePose.h"

class UActor;
class USkelModel;
// Geometry may be substituted while animation/joints stay on Skeletal.
USkelModel* GetRuneGeometry(UActor* actor);
UMesh* GetRuneDisplayMesh(UActor* actor);
mat4 GetRuneActorToWorld(UActor* actor, int depth = 0);
// Render vertices, joint queries and weapon sweeps must share this transform.
mat4 GetRuneMeshToWorld(UActor* actor, int depth = 0);
void UpdateRuneAttachments(UActor* actor);
void TickRuneDynamics(UActor* actor, float elapsed);

// Rune's USkelModel resource is not UE1's USkeletalMesh. Its serialized base is
// Primitive; UMesh supplies a transient adapter for the existing render path.
struct RuneVertex
{
	vec3 Point[2];
	int Joint[2];
	float Weight;
};

struct RuneTriangle
{
	uint16_t Index[3];
	uvec2b UV[3];
	uint8_t Group;
};

struct RuneMesh
{
	Array<RuneVertex> Vertices;
	Array<RuneTriangle> Triangles;
	int GroupFlags[16];
	NameString SkinNames[16];
	int DecimationCount;
	Array<uint8_t> DecimationData;
};

struct RuneJoint
{
	int Parent;
	int Children[4];
	NameString Name;
	int Group, Flags;
	Rotator BaseRotation;
	vec4 CollisionPlanes[6];
};

struct RuneSequence
{
	MeshAnimSeq Info;
	Array<uint8_t> Data;
};

struct RuneAnimFrame
{
	int16_t Sequence;
	NameString Event;
	BBox Bounds;
	Array<RuneJointPose> PreprocessJoints;
};

// Derived reference axes, owned by the model and never serialized.
struct RuneLegMirrorBasis {
    int Source=-1;
    bool Leg=false;
    mat3 Before=mat3::identity(),After=mat3::identity();
};

class USkelModel : public UMesh
{
public:
	using UMesh::UMesh;
	void Load(ObjectStream* stream) override;
	void Save(PackageStreamWriter* stream) override;
	Array<RuneJointPose> DecodeFrame(size_t sequence, int frame, bool validateToEnd = false, UActor* actor = nullptr) const;
    Array<RuneJointPose> SampleFrame(size_t sequence, float frame, bool loop) const;
    Array<RuneJointPose> SampleAnimation(UActor* actor) const;
    Array<RuneJointPose> ActorPose(UActor* actor) const;
    void CaptureTween(UActor* actor) const;
	Array<mat4> JointMatrices(const Array<RuneJointPose>& pose) const;
	Array<mat4> ActorJointMatrices(UActor* actor) const;
    BBox PoseBounds(UActor* actor) const;
	void InitializeDynamics(UActor* actor) const;
	void ApplyJointForce(UActor* actor, int joint, const vec3& force) const;
	void TickDynamics(UActor* actor, float elapsed) const;
	void ApplyDynamicPose(UActor* actor, Array<RuneJointPose>& pose) const;
	Array<vec3> SkinFrame(size_t mesh, size_t sequence, int frame, UActor* actor = nullptr) const;
	void PrepareMesh(int variant, NameString sequence, float frame, UActor* actor = nullptr);
	void ValidateAnimations() const;

	Array<RuneMesh> Meshes;
	Array<RuneJoint> Joints;
	Array<RuneSequence> Sequences;
	Array<RuneAnimFrame> Frames;
    mutable Array<RuneLegMirrorBasis> LegMirrorBasis;
	int NumSkins = 0, RootJoint = 0;
	vec3 PositionOffset;
	Rotator RotationOffset;
	uint32_t SerializedEnd = 0;
private:
    // Geometry-local influence bounds are immutable after the first query.
    // A skinned blend lies inside the union of its transformed influences.
    mutable Array<Array<BBox>> InfluenceBoxes;
};
