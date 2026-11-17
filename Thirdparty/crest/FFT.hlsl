// Crest Ocean System

// This file is subject to the MIT License as seen in the root of this folder structure (LICENSE)

// Inspired by https://github.com/speps/GX-EncinoWaves

// First SIZE constant must match FFT_KERNEL_0_RESOLUTION in FFTCompute.cs

// Must match CASCADE_COUNT in FFTCompute.cs
#define CASCADE_COUNT 16

[[vk::binding(0,0)]] Texture2DArray<float2> _InputH;
[[vk::binding(1,0)]] Texture2DArray<float2> _InputX;
[[vk::binding(2,0)]] Texture2DArray<float2> _InputZ;

#if !FINAL
[[vk::binding(3,0)]] RWTexture2DArray<float2> _Output1;
[[vk::binding(4,0)]] RWTexture2DArray<float2> _Output2;
[[vk::binding(5,0)]] RWTexture2DArray<float2> _Output3;
#else
// Write zero to W to clear garbage.
[[vk::binding(3,0)]] RWTexture2DArray<float4> _Output;
#endif

groupshared float2 _IntermediatesH[SIZE];
groupshared float2 _ScratchH[SIZE];
groupshared float2 _IntermediatesX[SIZE];
groupshared float2 _ScratchX[SIZE];
groupshared float2 _IntermediatesZ[SIZE];
groupshared float2 _ScratchZ[SIZE];

#if defined(SHADER_API_METAL)
// reversebits function appears to not make it across to Metal
#if !defined(reversebits)
uint reversebits( uint x )
{
	x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
	x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
	x = ((x >> 4) & 0x0f0f0f0fu) | ((x & 0x0f0f0f0fu) << 4);
	x = ((x >> 8) & 0x00ff00ffu) | ((x & 0x00ff00ffu) << 8);
	x = ((x >> 16) & 0xffffu) | ((x & 0xffffu) << 16);
	return x;
}
#endif
#endif

float2 NativeButterfly(uint coord,uint passIndex) {
 uint offset=1u<<passIndex; float phase=6.283185307179586*float(coord%offset)/float(2u*offset);
 float2 weight=float2(cos(phase),-sin(phase));return ((coord/offset)&1u)!=0u?-weight:weight;
}
void ButterflyPass(float2 butterfly, uint coord, uint passIndex, uint cascade)
{
	uint indexA, indexB;

	const uint offset = 1 << passIndex;
	if ((coord / offset) % 2 == 1)
	{
		indexA = coord - offset;
		indexB = coord;
	}
	else
	{
		indexA = coord;
		indexB = coord + offset;
	}

	if (passIndex == 0)
	{
		indexA = reversebits(indexA) >> (32 - PASSES);
		indexB = reversebits(indexB) >> (32 - PASSES);
	}

	const bool pingpong = (passIndex % 2) == 0;

	float2 valueA_H, valueB_H;
	float2 valueA_X, valueB_X;
	float2 valueA_Z, valueB_Z;
	if (pingpong)
	{
		valueA_H = _IntermediatesH[indexA];
		valueB_H = _IntermediatesH[indexB];

		valueA_X = _IntermediatesX[indexA];
		valueB_X = _IntermediatesX[indexB];

		valueA_Z = _IntermediatesZ[indexA];
		valueB_Z = _IntermediatesZ[indexB];
	}
	else
	{
		valueA_H = _ScratchH[indexA];
		valueB_H = _ScratchH[indexB];

		valueA_X = _ScratchX[indexA];
		valueB_X = _ScratchX[indexB];

		valueA_Z = _ScratchZ[indexA];
		valueB_Z = _ScratchZ[indexB];
	}

	const float2 weight = butterfly.xy;
	const float2 weightedValueH = weight * valueB_H.r + weight.gr * valueB_H.g * float2(-1.0, 1.0);
	const float2 weightedValueX = weight * valueB_X.r + weight.gr * valueB_X.g * float2(-1.0, 1.0);
	const float2 weightedValueZ = weight * valueB_Z.r + weight.gr * valueB_Z.g * float2(-1.0, 1.0);
	const float2 resultH = valueA_H + weightedValueH;
	const float2 resultX = valueA_X + weightedValueX;
	const float2 resultZ = valueA_Z + weightedValueZ;

	if (pingpong)
	{
		_ScratchH[coord] = resultH;
		_ScratchX[coord] = resultX;
		_ScratchZ[coord] = resultZ;
	}
	else
	{
		_IntermediatesH[coord] = resultH;
		_IntermediatesX[coord] = resultX;
		_IntermediatesZ[coord] = resultZ;
	}
}

float2 conj(float2 v)
{
	return float2(v.x, -v.y);
}

[numthreads(TX,TY,1)]
void ComputeFFT(uint3 id : SV_DispatchThreadID)
{
	const uint coord = id.CHANNEL;
#if !FINAL
	_IntermediatesH[coord] = conj(_InputH[id]);
	_IntermediatesX[coord] = conj(_InputX[id]);
	_IntermediatesZ[coord] = conj(_InputZ[id]);
#else
	_IntermediatesH[coord] = _InputH[id];
	_IntermediatesX[coord] = _InputX[id];
	_IntermediatesZ[coord] = _InputZ[id];
#endif

	[unroll(PASSES)]
	for (uint passIndex = 0; passIndex < PASSES; ++passIndex)
	{
		GroupMemoryBarrierWithGroupSync();
		ButterflyPass(NativeButterfly(coord,passIndex), coord, passIndex, id.z);
	}

	GroupMemoryBarrierWithGroupSync();

	const bool pingpong = (PASSES % 2) == 0;
	const float2 resultH = pingpong ? _IntermediatesH[coord] : _ScratchH[coord];
	const float2 resultX = pingpong ? _IntermediatesX[coord] : _ScratchX[coord];
	const float2 resultZ = pingpong ? _IntermediatesZ[coord] : _ScratchZ[coord];

#if !FINAL
	_Output1[id] = resultH;
	_Output2[id] = resultX;
	_Output3[id] = resultZ;
#else
	const float sign = ((id.x + id.y) % 2) == 1 ? -1.0 : 1.0;
	const float3 res = float3(sign * resultX.x, sign * resultH.x, sign * resultZ.x);
	// Write zero to W to clear garbage.
	_Output[id] = float4(sign * resultX.x, sign * resultH.x, sign * resultZ.x, 0.0);
#endif
}