/*
    Copyright 2019-2026 Hydr8gon

    This file is part of NooDS.

    NooDS is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NooDS is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NooDS. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <mutex>
#include <queue>

#include "../defines.h"

class Core;

class Gpu {
public:
    Gpu(Core *core);
    ~Gpu();

    void init();
    void saveState(FILE *file);
    void loadState(FILE *file);

    bool getFrame(uint32_t *out, bool gbaCrop);
    // Single display consumer only. The borrowed buffer stays alive until
    // releaseFrameNative(), after the synchronous texture upload has copied it.
    bool acquireFrameNative(uint32_t *&pixels);
    void releaseFrameNative();
#ifdef NOODS_XENON
    // Return the native DS image in the RGB555 index format consumed by the
    // Xbox picture-adjustment lookup. Non-native/ghosting modes retain the
    // upstream RGB8 path internally so their final output stays exact.
    bool getFrameXenon(uint16_t *out, bool gbaCrop);
#endif
    void invalidate3D() { dirty3D |= BIT(0); }
#ifdef NOODS_XENON
    // Apply a new render cadence only at the next frame's 3D line-zero
    // boundary. This keeps live Turbo changes from splitting a frame in half.
    void requestFrameSkip(int skip);
#endif

    void gbaScanline240();
    void gbaScanline308();
    void scanline256();
    void scanline355();

    uint16_t readDispStat(bool cpu) { return dispStat[cpu]; }
    uint16_t readVCount() { return vCount; }
    uint32_t readDispCapCnt() { return dispCapCnt; }
    uint16_t readPowCnt1() { return powCnt1; }

    void writeDispStat(bool cpu, uint16_t mask, uint16_t value);
    void writeDispCapCnt(uint32_t mask, uint32_t value);
    void writePowCnt1(uint16_t mask, uint16_t value);

private:
    Core *core;

    struct Buffers {
    Buffers(): framebuffer(nullptr), hiRes3D(nullptr), top3D(false) {}

        uint32_t *framebuffer;
        uint32_t *hiRes3D;
        bool top3D;
    };

    bool canQueueFrame();
    std::queue<Buffers> framebuffers;
    std::atomic<bool> ready;
    bool nativeFrameBorrowed; // display-consumer owned
    std::mutex mutex;

    std::atomic<bool> running;
    std::atomic<int> drawing;
    std::thread *thread;

    int frames;
#ifdef NOODS_XENON
    std::atomic<int> requestedFrameSkip;
    int activeFrameSkip;
    // A skipped frontend frame can still need fresh internal pixels when a
    // game captures engine A into VRAM. Keep guest-visible capture rendering
    // independent from framebuffer handoff cadence.
    bool captureRenderFrame;
    bool capture3DFrame;
#endif
    bool gbaBlock;
    bool displayCapture;
    uint8_t dirty3D;

    uint16_t dispStat[2];
    uint16_t vCount;
    uint32_t dispCapCnt;
    uint16_t powCnt1;

    static uint32_t rgb5ToRgb8(uint32_t color);
    static uint32_t rgb6ToRgb8(uint32_t color);
    static uint16_t rgb6ToRgb5(uint32_t color);

    void drawGbaThreaded();
    void drawThreaded();
};
