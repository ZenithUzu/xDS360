#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply the reproducible native RGB6/Xenos handoff to converted NooDS."""
from pathlib import Path

def edit(path, old, new, count=1):
    text = path.read_text()
    if text.count(old) != count:
        raise ValueError('%s: expected %d occurrences of %r' % (path, count, old[:60]))
    path.write_text(text.replace(old, new))

def apply_native_video(out):
    gpu = out / 'core/gpu/gpu.cpp'
    header = out / 'core/gpu/gpu.h'
    edit(header, '    bool getFrame(uint32_t *out, bool gbaCrop);',
         '''    bool getFrame(uint32_t *out, bool gbaCrop);
    // Single display consumer only. The borrowed buffer stays alive until
    // releaseFrameNative(), after the synchronous texture upload has copied it.
    bool acquireFrameNative(uint32_t *&pixels);
    void releaseFrameNative();''')
    edit(header, '    std::queue<Buffers> framebuffers;',
         '    bool canQueueFrame();\n    std::queue<Buffers> framebuffers;')
    edit(header, '    std::atomic<bool> ready;',
         '    std::atomic<bool> ready;\n    bool nativeFrameBorrowed; // display-consumer owned')
    edit(gpu, '    // Mark the thread as not drawing to start',
         '    nativeFrameBorrowed = false;\n    // Mark the thread as not drawing to start')
    edit(gpu, 'framebuffers.size() < 2', 'canQueueFrame()', 2)
    edit(gpu, 'bool Gpu::getFrame(uint32_t *out, bool gbaCrop) {', '''bool Gpu::canQueueFrame() {
    std::lock_guard<std::mutex> guard(mutex);
    return framebuffers.size() < 2;
}

bool Gpu::acquireFrameNative(uint32_t *&pixels) {
    // No lossy conversion: the lower 18 bits hold R6/G6/B6. Do not borrow
    // fallback modes, which have different geometry or temporal blending.
    if (core->gbaMode || Settings::screenGhost || Settings::highRes3D ||
        Settings::screenFilter == 1 || nativeFrameBorrowed) return false;
    std::lock_guard<std::mutex> guard(mutex);
    if (framebuffers.empty()) return false;
    pixels = framebuffers.front().framebuffer;
    nativeFrameBorrowed = true;
    return true;
}

void Gpu::releaseFrameNative() {
    if (!nativeFrameBorrowed) return;
    std::lock_guard<std::mutex> guard(mutex);
    Buffers &buffers = framebuffers.front();
    delete[] buffers.framebuffer;
    delete[] buffers.hiRes3D;
    framebuffers.pop();
    ready.store(!framebuffers.empty());
    nativeFrameBorrowed = false;
}

bool Gpu::getFrame(uint32_t *out, bool gbaCrop) {
    if (nativeFrameBorrowed) return false;''')
    edit(gpu, 'bool Gpu::getFrameXenon(uint16_t *out, bool gbaCrop) {',
         'bool Gpu::getFrameXenon(uint16_t *out, bool gbaCrop) {\n    if (nativeFrameBorrowed) return false;')
    ui = out / 'ui/console/console_ui.cpp'
    ui_header = out / 'ui/console/console_ui.h'
    edit(ui_header, '    static bool gbaMode;',
         '    static bool gbaMode;\n    static int gpuNativeVideo;\n    static bool useNativeVideo();')
    edit(ui_header, 'static void *createTexture(uint32_t *data, int width, int height);',
         'static void *createTexture(uint32_t *data, int width, int height, bool nativeRgb6 = false);')
    edit(ui, '        Setting("showFpsCounter", &showFpsCounter, false),',
         '        Setting("gpuNativeVideo", &gpuNativeVideo, false),\n        Setting("showFpsCounter", &showFpsCounter, false),')
    edit(ui, '        const bool freshFrame = core->gpu.getFrame(framebuffer, gbaMode);', '''        uint32_t *frameData = framebuffer;
        const bool nativeVideo = useNativeVideo();
        const bool freshFrame = nativeVideo ? core->gpu.acquireFrameNative(frameData) :
            core->gpu.getFrame(framebuffer, gbaMode);
        const bool borrowedFrame = nativeVideo && freshFrame;''')
    edit(ui, '            if (ScreenLayout::screenArrangement != 3 || ScreenLayout::screenSizing < 2) {',
         '            if ((freshFrame || topTexture) && (ScreenLayout::screenArrangement != 3 || ScreenLayout::screenSizing < 2)) {')
    edit(ui, '            if (ScreenLayout::screenArrangement != 3 || ScreenLayout::screenSizing == 2) {',
         '            if ((freshFrame || botTexture) && (ScreenLayout::screenArrangement != 3 || ScreenLayout::screenSizing == 2)) {')
    edit(ui, 'createTexture(&framebuffer[0], 256 << shift, 192 << shift)',
         'createTexture(frameData, 256 << shift, 192 << shift, borrowedFrame)')
    edit(ui, 'createTexture(&framebuffer[(256 * 192) << (shift * 2)], 256 << shift, 192 << shift)',
         'createTexture(&frameData[(256 * 192) << (shift * 2)], 256 << shift, 192 << shift, borrowedFrame)')
    edit(ui, '        // Draw the FPS counter if enabled', '''        // LockRect/UnlockRect has copied the borrowed image into textures.
        // Release before input, menus or Present; no core pointer escapes here.
        if (borrowedFrame) core->gpu.releaseFrameNative();

        // Draw the FPS counter if enabled''')
