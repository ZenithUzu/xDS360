// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Native NooDS RGB6 is packed as RRRRRR GGGGGG BBBBBB + internal flags.
// Sampling the packed bytes with hardware bilinear filtering corrupts carries
// between channels. Decode each texel BEFORE any interpolation.
#define XDS_RGB6_SHADER_COMMON \
    "sampler image : register(s0);\n" \
    "float4 tint : register(c0);\n" \
    "float4 imageSize : register(c1);\n" \
    "float4 decodeRGB6(float2 uv) {\n" \
    "  float3 v = floor(tex2D(image, uv).bgr * 255.0 + 0.5);\n" \
    "  float3 c;\n" \
    "  c.r = v.r - floor(v.r / 64.0) * 64.0;\n" \
    "  c.g = floor(v.r / 64.0) + (v.g - floor(v.g / 16.0) * 16.0) * 4.0;\n" \
    "  c.b = floor(v.g / 16.0) + (v.b - floor(v.b / 4.0) * 4.0) * 16.0;\n" \
    "  return float4(floor(c * (255.0 / 63.0) + 0.0001) / 255.0, 1.0);\n" \
    "}\n"

static const char *kNativePointShader = XDS_RGB6_SHADER_COMMON
    "float4 main(float2 uv : TEXCOORD0) : COLOR { return decodeRGB6(uv) * tint; }\n";

static const char *kNativeLinearShader = XDS_RGB6_SHADER_COMMON
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float2 p = uv * imageSize.xy - 0.5;\n"
    "  float2 f = frac(p);\n"
    "  float2 base = (floor(p) + 0.5) * imageSize.zw;\n"
    "  float4 a = decodeRGB6(base);\n"
    "  float4 b = decodeRGB6(base + float2(imageSize.z, 0));\n"
    "  float4 c = decodeRGB6(base + float2(0, imageSize.w));\n"
    "  float4 d = decodeRGB6(base + imageSize.zw);\n"
    "  return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y) * tint;\n"
    "}\n";
#undef XDS_RGB6_SHADER_COMMON

// Reference used by the simulator and exhaustive colour tests. The Xbox
// path sends the unmodified word to a texture and runs the HLSL above.
inline unsigned int xdsNativeRgb6ToRgb8(unsigned int value) {
    return 0xFF000000u | ((value & 63u) * 255u / 63u) |
        ((((value >> 6) & 63u) * 255u / 63u) << 8) |
        ((((value >> 12) & 63u) * 255u / 63u) << 16);
}
