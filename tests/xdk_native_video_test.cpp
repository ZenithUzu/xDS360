// SPDX-License-Identifier: GPL-3.0-or-later
#include "core.h"
#include "settings.h"
#include "../xdk/frontend/xds_native_video.h"
#include <cstdio>
#include <cmath>
#include <cstring>

namespace {
uint32_t converted[256 * 192 * 2];
uint32_t scratch[256 * 192 * 2];

uint32_t decodeShaderReference(uint32_t value) {
    // Full precision HLSL byte recovery and unpacking, including UNORM input.
    const float byteR = std::floor(((value & 255u) / 255.0f) * 255.0f + 0.5f);
    const float byteG = std::floor((((value >> 8) & 255u) / 255.0f) * 255.0f + 0.5f);
    const float byteB = std::floor((((value >> 16) & 255u) / 255.0f) * 255.0f + 0.5f);
    const float r = byteR - std::floor(byteR / 64.0f) * 64.0f;
    const float g = std::floor(byteR / 64.0f) + (byteG - std::floor(byteG / 16.0f) * 16.0f) * 4.0f;
    const float b = std::floor(byteG / 16.0f) + (byteB - std::floor(byteB / 4.0f) * 4.0f) * 16.0f;
    const unsigned ri = (unsigned)std::floor(r * (255.0f / 63.0f) + 0.0001f);
    const unsigned gi = (unsigned)std::floor(g * (255.0f / 63.0f) + 0.0001f);
    const unsigned bi = (unsigned)std::floor(b * (255.0f / 63.0f) + 0.0001f);
    return 0xFF000000u | ri | (gi << 8) | (bi << 16);
}

bool nextFrame(Core &core) {
    const uint32_t before = core.completedFrames;
    for (int slices = 0; slices < 4096; ++slices) {
        core.runCore();
        if (core.completedFrames != before) return true;
    }
    return false;
}

bool testColours() {
    for (uint32_t rgb = 0; rgb < (1u << 18); ++rgb) {
        for (int flags = 0; flags < 2; ++flags) {
            const uint32_t word = rgb | (flags ? 0xFFFC0000u : 0u);
            if (decodeShaderReference(word) != xdsNativeRgb6ToRgb8(rgb)) return false;
        }
    }
    // The linear shader blends expanded colours, never the packed bytes.
    // Check a carry across R/G at all 256x256 subpixel weights.
    const uint32_t raw[4] = {63u, 64u, 4095u, 4096u};
    for (unsigned y = 0; y < 256; ++y) for (unsigned x = 0; x < 256; ++x) {
        const double wx = x / 255.0, wy = y / 255.0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            double expected = 0, actual = 0;
            for (unsigned pixel = 0; pixel < 4; ++pixel) {
                const double weight = ((pixel & 1) ? wx : 1 - wx) * ((pixel & 2) ? wy : 1 - wy);
                expected += ((xdsNativeRgb6ToRgb8(raw[pixel]) >> (channel * 8)) & 255u) * weight;
                actual += ((decodeShaderReference(raw[pixel]) >> (channel * 8)) & 255u) * weight;
            }
            if (std::fabs(actual - expected) > 0.00001) return false;
        }
    }
    return true;
}

void benchmarkHandoff(const char *rom) {
    Core *native = new Core(rom), *reference = new Core(rom);
    long long cpuTime = 0, nativeTime = 0;
    for (int frame = 0; frame < 240; ++frame) {
        if (!nextFrame(*native) || !nextFrame(*reference)) return;
        long long start = xds::clockMicroseconds();
        reference->gpu.getFrame(converted, false);
        std::memcpy(scratch, converted, sizeof(scratch)); // stand-in for texture upload
        xds::compilerBarrier();
        cpuTime += xds::clockMicroseconds() - start;
        uint32_t *raw = 0;
        start = xds::clockMicroseconds();
        if (native->gpu.acquireFrameNative(raw)) {
            std::memcpy(scratch, raw, sizeof(scratch));
            xds::compilerBarrier();
            native->gpu.releaseFrameNative();
        }
        nativeTime += xds::clockMicroseconds() - start;
    }
    std::printf("HANDOFF_CPU_ONLY frames=240 RGB8_us=%lld native_RGB6_us=%lld (excludes GPU shader cost, not Xbox FPS)\n",
                cpuTime, nativeTime);
    delete native;
    delete reference;
}

bool testHandoff(const char *rom) {
    Core *native = new Core(rom), *reference = new Core(rom);
    for (int frame = 0; frame < 32; ++frame) {
        // Exercise display swapping as well as both normal sampling modes.
        Settings::screenFilter = frame & 1 ? 2 : 0;
        native->gpu.writePowCnt1(1u << 15, (frame & 1) << 15);
        reference->gpu.writePowCnt1(1u << 15, (frame & 1) << 15);
        if (!nextFrame(*native) || !nextFrame(*reference)) return false;
        uint32_t *raw = 0;
        if (!native->gpu.acquireFrameNative(raw) || !reference->gpu.getFrame(converted, false)) return false;
        uint32_t *second = 0;
        if (native->gpu.acquireFrameNative(second) || native->gpu.getFrame(scratch, false) ||
            native->gpu.getFrameXenon((uint16_t*)scratch, false)) return false;
        for (unsigned i = 0; i < 256u * 192u * 2u; ++i)
            if (xdsNativeRgb6ToRgb8(raw[i]) != converted[i]) return false;
        native->gpu.releaseFrameNative();
        native->gpu.releaseFrameNative(); // no double-free
    }
    // The producer may run ahead while a texture upload borrows the front.
    if (!nextFrame(*native)) return false;
    uint32_t *raw = 0;
    if (!native->gpu.acquireFrameNative(raw)) return false;
    std::memcpy(scratch, raw, sizeof(scratch));
    for (int i = 0; i < 8; ++i) if (!nextFrame(*native)) return false;
    if (std::memcmp(scratch, raw, sizeof(scratch))) return false;
    native->gpu.releaseFrameNative();
    Settings::screenGhost = 1;
    if (native->gpu.acquireFrameNative(raw)) return false;
    Settings::screenGhost = 0;
    Settings::highRes3D = 1;
    if (native->gpu.acquireFrameNative(raw)) return false;
    Settings::highRes3D = 0;
    Settings::screenFilter = 1;
    if (native->gpu.acquireFrameNative(raw)) return false;
    Settings::screenFilter = 0;
    native->gbaMode = true;
    if (native->gpu.acquireFrameNative(raw)) return false;
    native->gbaMode = false;
    delete native;
    delete reference;
    return true;
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    Settings::directBoot = Settings::romInRam = 1;
    Settings::fpsLimiter = Settings::frameskip = Settings::threaded2D = Settings::threaded3D = 0;
    Settings::highRes3D = Settings::screenGhost = Settings::emulateAudio = 0;
    Settings::savesFolder = Settings::statesFolder = Settings::cheatsFolder = 0;
    Settings::screenFilter = Settings::dsiMode = Settings::arm7Hle = 0;
    if (!testColours()) { std::puts("FAIL native colour/shader reference"); return 1; }
    if (!testHandoff(argv[1])) { std::puts("FAIL native frame ownership/parity"); return 1; }
    benchmarkHandoff(argv[1]);
    std::puts("PASS native video: 524288 colours/flags; 65536 bilinear weights; 32 frame comparisons; ownership and fallback modes");
    return 0;
}
