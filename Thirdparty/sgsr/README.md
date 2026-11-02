# Snapdragon GSR v1

Source: https://github.com/SnapdragonGameStudios/snapdragon-gsr
Revision: d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e
Upstream path: sgsr/v1/include/glsl/sgsr1_shader_mobile.frag
BSD-3-Clause; LICENSE and original shader retained.

SGSRShader.h adapts the RGB/green-edge mode into the existing Vulkan presentation
pass. Uniforms/UVs come from the renderer, sharpening is reduced from 2 to 1.5,
divisions are guarded, and the delta clamp preserves source bloom highlights
above 1. Filtering occurs in the legacy scene color space before the existing
color/gamma/output conversion. It needs no extra render pass or history.
The game's HUD currently shares the scene resolution; Android controls stay native.
