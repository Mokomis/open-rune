#pragma once

#include <cstddef>
#include <surrealgpu/vulkanobjects.h>
#include "Math/mat.h"
#include "Render/ShadowSettings.h"

class VulkanRenderDevice;

struct SceneVertex
{
	uint32_t Flags;
	vec3 Position;
	vec2 TexCoord;
	vec2 TexCoord2;
	vec2 TexCoord3;
	vec2 TexCoord4;
	vec4 Color;
	ivec4 TextureBinds;
	vec3 Normal;
};

// std430: explicit vec4 lanes keep the host and shader layouts identical.
struct MeshLightPacket
{
	vec4 AmbientScale;
	ivec4 Meta;
	struct { vec4 PositionRadius; vec4 Color; } Lights[8];
    vec4 SurfaceOrigin, SurfaceU, SurfaceV; // xyz mapping; w lanes hold surface normal
    vec4 Material; // specular strength, exponent, reserved
    ivec4 MaterialMaps; // normal/roughness descriptor, enabled
    vec4 ViewCamera;
    vec4 SurfaceTangent; // xyz geometric texture tangent, w conservative contact mask
    vec4 SurfaceBitangent; // xyz geometric texture bitangent, w basis valid
};
static_assert(sizeof(MeshLightPacket) == 416, "Mesh light buffer layout");

// Only mat4/vec4 members: identical std140 uniform and std430 storage layouts.
struct ShadowLightingPacket
{
    struct { mat4 WorldToShadow; vec4 LightPosition; } Shadows[RuneShadows::Capacity];
    vec4 CameraPosition; // w: remastered mode
    mat4 WaterWorldToClip;
    vec4 WaterParameters; // time, plane height, valid reflection, reserved
    vec4 ContactCenters[4]; // xyz ground hit, w inverse squared footprint radius
    vec4 ContactPlanes[4]; // xyz ground normal, w strength (zero disables)
};
static_assert(sizeof(ShadowLightingPacket) == 80*RuneShadows::Capacity+224, "Shadow uniform layout");
static_assert(offsetof(ShadowLightingPacket, CameraPosition) == 80*RuneShadows::Capacity, "Shadow camera offset");
static_assert(offsetof(ShadowLightingPacket, WaterWorldToClip) == 80*RuneShadows::Capacity+16, "Water transform offset");
static_assert(offsetof(ShadowLightingPacket, WaterParameters) == 80*RuneShadows::Capacity+80, "Water parameters offset");
static_assert(offsetof(ShadowLightingPacket, ContactCenters) == 80*RuneShadows::Capacity+96, "Contact centers offset");
static_assert(offsetof(ShadowLightingPacket, ContactPlanes) == 80*RuneShadows::Capacity+160, "Contact planes offset");

struct ScenePushConstants
{
	mat4 objectToProjection;
	mat4 objectToView;
	vec4 nearClip;
	uint32_t hitIndex;
	uint32_t padding1, padding2, padding3;
};

struct PresentPushConstants
{
	float Contrast;
	float Saturation;
	float Brightness;
	float HdrScale;
	vec4 GammaCorrection;
	vec4 TextureRow0 = vec4(1, 0, 0, 0);
	vec4 TextureRow1 = vec4(0, 1, 0, 0);
	vec4 SourceSize;
	int Upscale = 0;
	float AdaptiveSharpness = 0.0f;
	float ShadowLift = 0.0f;
	float AmbientOcclusion = 0.0f;
	int AmbientSamples = 0;
};

struct BloomPushConstants
{
	float SampleWeights[8];
	float Intensity = 1.0f;
	float Threshold = 1.0f;
};

class ShaderManager
{
public:
	ShaderManager(VulkanRenderDevice* renderer);
	~ShaderManager();

	struct SceneShaders
	{
		std::vector<uint32_t> VertexShader;
		std::vector<uint32_t> FragmentShader;
		std::vector<uint32_t> FragmentShaderAlphaTest;
        std::vector<uint32_t> FragmentShaderCoverage;
	} Scene, SceneNoHit, Water, WaterNoHit;
    std::vector<uint32_t> ShadowVertex, ShadowFragment;

	struct
	{
		std::vector<uint32_t> VertexShader;
		std::vector<uint32_t> FragmentPresentShader[48];
	} Postprocess;

	struct
	{
		std::vector<uint32_t> Extract;
		std::vector<uint32_t> Combine;
		std::vector<uint32_t> BlurVertical;
		std::vector<uint32_t> BlurHorizontal;
	} Bloom;

	static std::string LoadShaderCode(const std::string& filename, const std::string& defines = {});

private:
	VulkanRenderDevice* renderer = nullptr;
};
