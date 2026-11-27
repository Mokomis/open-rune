#pragma once

#include <surrealgpu/vulkanobjects.h>

struct TextureInfo;
class UnrealMipmap;
struct TextureColor;
enum class TextureFormat : uint32_t;

class TextureUploader
{
public:
	TextureUploader(VkFormat format) : Format(format) { }
	virtual ~TextureUploader() = default;

	virtual int GetUploadSize(int x, int y, int w, int h) = 0;
	virtual void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) = 0;

	VkFormat GetVkFormat() const { return Format; }

	static TextureUploader* GetUploader(TextureFormat format);

	// Set once at device creation: true when the GPU lacks textureCompressionBC
	// (most Mali/PowerVR). BC1-BC3 are then decoded to RGBA8 on the CPU.
	static bool DecodeBC;

private:
	VkFormat Format;
};

class TextureUploader_P8 : public TextureUploader
{
public:
	TextureUploader_P8() : TextureUploader(VK_FORMAT_R8G8B8A8_UNORM) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) override;
};

class TextureUploader_BGRA8_LM : public TextureUploader
{
public:
	TextureUploader_BGRA8_LM() : TextureUploader(VK_FORMAT_R8G8B8A8_UNORM) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) override;
};

class TextureUploader_RGB10A2 : public TextureUploader
{
public:
	TextureUploader_RGB10A2() : TextureUploader(VK_FORMAT_R16G16B16A16_UNORM) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) override;
};

class TextureUploader_RGB10A2_UI : public TextureUploader
{
public:
	TextureUploader_RGB10A2_UI() : TextureUploader(VK_FORMAT_R16G16B16A16_UINT) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) override;
};

class TextureUploader_RGB10A2_LM : public TextureUploader
{
public:
	TextureUploader_RGB10A2_LM() : TextureUploader(VK_FORMAT_R16G16B16A16_UNORM) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) override;
};

class TextureUploader_Simple : public TextureUploader
{
public:
	TextureUploader_Simple(VkFormat format, int bytesPerPixel) : TextureUploader(format), BytesPerPixel(bytesPerPixel) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) override;

private:
	int BytesPerPixel;
};

class TextureUploader_4x4Block : public TextureUploader
{
public:
	TextureUploader_4x4Block(VkFormat format, int bytesPerBlock) : TextureUploader(format), BytesPerBlock(bytesPerBlock) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) override;

private:
	int BytesPerBlock;
};

// CPU fallback for S3TC/DXT blocks (BC1, BC2, BC3) on GPUs without BC sampling.
class TextureUploader_DecodeBC : public TextureUploader
{
public:
	TextureUploader_DecodeBC(int bcVersion) : TextureUploader(VK_FORMAT_R8G8B8A8_UNORM), Version(bcVersion) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) override;

	// Decodes one 4x4 block to 16 RGBA8 texels, row-major.
	static void DecodeBlock(int bcVersion, const uint8_t* block, uint8_t rgba[64]);

private:
	int Version;
};

class TextureUploader_2DBlock : public TextureUploader
{
public:
	TextureUploader_2DBlock(VkFormat format, int blockX, int blockY, int bytesPerBlock) : TextureUploader(format), BlockX(blockX), BlockY(blockY), BytesPerBlock(bytesPerBlock) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, TextureColor* palette, bool masked) override;

private:
	int BlockX;
	int BlockY;
	int BytesPerBlock;
};
