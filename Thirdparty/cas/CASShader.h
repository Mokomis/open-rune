// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: MIT
// Adapted sharpen-only CAS, full-precision per-channel five-tap path.
// See README.md and LICENSE. Adds a blend control and epsilon for black input.
#pragma once
inline constexpr const char* CASShader = R"cas(
vec3 adaptiveSharpen(vec2 uv)
{
    vec3 e = texture(texSampler, uv).rgb;
    if (AdaptiveSharpness <= 0.0) return e;
    vec2 dx = vec2(SourceSize.x, 0.0);
    vec2 dy = vec2(0.0, SourceSize.y);
    vec3 b = texture(texSampler, uv - dy).rgb;
    vec3 d = texture(texSampler, uv - dx).rgb;
    vec3 f = texture(texSampler, uv + dx).rgb;
    vec3 h = texture(texSampler, uv + dy).rgb;
    vec3 mn = min(min(min(d, e), min(f, b)), h);
    vec3 mx = max(max(max(d, e), max(f, b)), h);
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(0.0001)), 0.0, 1.0));
    vec3 weight = amp * (-1.0 / 8.0);
    vec3 sharpened = clamp((e + (b + d + f + h) * weight) / (1.0 + 4.0 * weight), 0.0, 1.0);
    return mix(e, sharpened, AdaptiveSharpness);
}
)cas";
