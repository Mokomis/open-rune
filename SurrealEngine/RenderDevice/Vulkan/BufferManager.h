#pragma once

#include "ShaderManager.h"

class VulkanRenderDevice;
struct SceneVertex;

class BufferManager
{
public:
	BufferManager(VulkanRenderDevice* renderer);
	~BufferManager();

	std::unique_ptr<VulkanBuffer> SceneVertexBuffer;
	std::unique_ptr<VulkanBuffer> SceneIndexBuffer;
	std::unique_ptr<VulkanBuffer> UploadBuffer;

	std::unique_ptr<VulkanBuffer> MeshLightBuffer;
	std::unique_ptr<VulkanDescriptorSet> MeshLightSet, ReflectionLightSet;
	MeshLightPacket* MeshLights = nullptr;
    std::unique_ptr<VulkanBuffer> ShadowLightingBuffer;
    ShadowLightingPacket* ShadowLighting = nullptr;
	static constexpr int MaxMeshLights = 4096;
	int MeshLightCount = 0;

	SceneVertex* SceneVertices = nullptr;
	uint32_t* SceneIndexes = nullptr;
	uint8_t* UploadData = nullptr;

	static const int SceneVertexBufferSize = 1 * 1024 * 1024;
	static const int SceneIndexBufferSize = 1 * 1024 * 1024;

	static const int UploadBufferSize = 64 * 1024 * 1024;

private:
	void CreateSceneVertexBuffer();
	void CreateSceneIndexBuffer();
	void CreateUploadBuffer();

	VulkanRenderDevice* renderer = nullptr;
};
