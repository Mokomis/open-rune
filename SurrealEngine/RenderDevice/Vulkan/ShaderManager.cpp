
#include "Precomp.h"
#include "ShaderManager.h"
#include "FileResource.h"
#include "VulkanRenderDevice.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <surrealgpu/vulkanbuilders.h>

ShaderManager::ShaderManager(VulkanRenderDevice* renderer) : renderer(renderer)
{
	GLSLCompiler::Init();
    // Permit packed arithmetic for bounded lighting values. Geometry and shadow
    // coordinates remain high precision; the reference path supports A/B checks.
    bool fullPrecisionLighting=commandline->GetArgInt("","--render-full-precision-lighting",0)!=0;
    bool earlyDepth=commandline->GetArgInt("","--render-opaque-early-depth",1)!=0;
    bool cullLighting=commandline->GetArgInt("","--render-cull-lighting",1)!=0;
    LogMessage("RUNE_LIGHTING_CULL enabled="+std::to_string(cullLighting));
    LogMessage("RUNE_OPAQUE_EARLY_DEPTH enabled="+std::to_string(earlyDepth));
    LogMessage("RUNE_LIGHTING_PRECISION relaxed="+std::to_string(!fullPrecisionLighting));

	auto compileScene = [&](SceneShaders& shaders, bool noHit, bool water = false)
	{
		std::string defines = "#extension GL_EXT_nonuniform_qualifier : enable\n";
		defines += fullPrecisionLighting ? "#define LIGHTP highp\n" : "#define LIGHTP mediump\n";
        if(cullLighting) defines += "#define CULL_LIGHTING\n";
        if(renderer->UniformShadowData)defines+="#define UNIFORM_SHADOW_DATA\n";
        if(commandline->GetArgInt("","--render-shadow-receivers",1)!=0) defines += "#define SHADOW_RECEIVER_BOUNDS\n";
		if (noHit) defines += "#define NO_HIT_OUTPUT\n";
        if (water) defines += "#define WATER_SURFACE\n";
		shaders.VertexShader = GLSLCompiler().Type(ShaderType::Vertex)
			.AddSource("shaders/Scene.vert", LoadShaderCode("shaders/Scene.vert", defines)).Compile(renderer->Device.get());
		shaders.FragmentShader = GLSLCompiler().Type(ShaderType::Fragment)
			.AddSource("shaders/Scene.frag", LoadShaderCode("shaders/Scene.frag", defines + (earlyDepth ? "#define NO_DISCARD\n" : ""))).Compile(renderer->Device.get());
		shaders.FragmentShaderCoverage = GLSLCompiler().Type(ShaderType::Fragment)
			.AddSource("shaders/Scene.frag", LoadShaderCode("shaders/Scene.frag", defines)).Compile(renderer->Device.get());
		shaders.FragmentShaderAlphaTest = GLSLCompiler().Type(ShaderType::Fragment)
			.AddSource("shaders/Scene.frag", LoadShaderCode("shaders/Scene.frag", defines + "#define ALPHATEST\n")).Compile(renderer->Device.get());
	};
	compileScene(Scene, false);
	compileScene(SceneNoHit, true);
    compileScene(Water, false, true);
    compileScene(WaterNoHit, true, true);
    ShadowVertex = GLSLCompiler().Type(ShaderType::Vertex).AddSource("shaders/Shadow.vert", LoadShaderCode("shaders/Shadow.vert")).Compile(renderer->Device.get());
    ShadowFragment = GLSLCompiler().Type(ShaderType::Fragment).AddSource("shaders/Shadow.frag", LoadShaderCode("shaders/Shadow.frag", "#extension GL_EXT_nonuniform_qualifier : enable\n")).Compile(renderer->Device.get());

	Postprocess.VertexShader = GLSLCompiler()
		.Type(ShaderType::Vertex)
		.AddSource("shaders/PPStep.vert", LoadShaderCode("shaders/PPStep.vert"))
		.Compile(renderer->Device.get());

	static const char* transferFunctions[2] = { nullptr, "HDR_MODE" };
	static const char* gammaModes[2] = { "GAMMA_MODE_D3D9", "GAMMA_MODE_XOPENGL" };
	static const char* colorModes[4] = { nullptr, "COLOR_CORRECT_MODE0", "COLOR_CORRECT_MODE1", "COLOR_CORRECT_MODE2" };
	for (int i = 0; i < 48; i++)
	{
		std::string defines;
		if (i >= 16) defines += "#define AMBIENT_OCCLUSION\n";
		if (i >= 32) defines += "#define DEPTH_MULTISAMPLED\n";
		if (transferFunctions[i & 1]) defines += std::string("#define ") + transferFunctions[i & 1] + "\r\n";
		if (gammaModes[(i >> 1) & 1]) defines += std::string("#define ") + gammaModes[(i >> 1) & 1] + "\r\n";
		if (colorModes[(i >> 2) & 3]) defines += std::string("#define ") + colorModes[(i >> 2) & 3] + "\r\n";

		Postprocess.FragmentPresentShader[i] = GLSLCompiler()
			.Type(ShaderType::Fragment)
			.AddSource("shaders/Present.frag", LoadShaderCode("shaders/Present.frag", defines))
			.Compile(renderer->Device.get());
	}

	Bloom.Extract = GLSLCompiler()
		.Type(ShaderType::Fragment)
		.AddSource("shaders/BloomExtract.frag", LoadShaderCode("shaders/BloomExtract.frag"))
		.Compile(renderer->Device.get());

	Bloom.Combine = GLSLCompiler()
		.Type(ShaderType::Fragment)
		.AddSource("shaders/BloomCombine.frag", LoadShaderCode("shaders/BloomCombine.frag"))
		.Compile(renderer->Device.get());

	Bloom.BlurVertical = GLSLCompiler()
		.Type(ShaderType::Fragment)
		.AddSource("shaders/BlurVertical.frag", LoadShaderCode("shaders/Blur.frag", "#define BLUR_VERTICAL"))
		.Compile(renderer->Device.get());

	Bloom.BlurHorizontal = GLSLCompiler()
		.Type(ShaderType::Fragment)
		.AddSource("shaders/BlurHorizontal.frag", LoadShaderCode("shaders/Blur.frag", "#define BLUR_HORIZONTAL"))
		.Compile(renderer->Device.get());
}

ShaderManager::~ShaderManager()
{
	GLSLCompiler::Deinit();
}

std::string ShaderManager::LoadShaderCode(const std::string& filename, const std::string& defines)
{
	const char* shaderversion = R"(
		#version 450
		#extension GL_ARB_separate_shader_objects : enable
	)";
	return shaderversion + defines + "\r\n#line 1\r\n" + FileResource::readAllText(filename);
}
