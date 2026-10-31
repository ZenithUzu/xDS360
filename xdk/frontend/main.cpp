// SPDX-License-Identifier: GPL-3.0-or-later
// xDS360 platform layer for NooDS's console UI (the one NooDS uses on Wii U,
// see its src/ui/console/main_wiiu.cpp): Direct3D drawing, XInput, XAudio2,
// drive mounting and a log. The menus themselves are NooDS's ConsoleUI.
#define NOMINMAX
#include <xtl.h>
#include <xaudio2.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "../noods/ui/console/console_ui.h"
#include "../noods/core/settings.h"
#include "xds_platform.h"
#include "xds_unistd.h"
#include "xds_native_video.h"

// Kernel call that makes a device reachable by a name, such as the hard drive
// as "hdd:". Same layout as the kernel's ANSI_STRING.
struct KernelString {
    unsigned short length;
    unsigned short maximumLength;
    const char *buffer;
};
extern "C" long ObCreateSymbolicLink(KernelString *link, KernelString *device);

extern uint8_t _binary_src_ui_console_images_font_bmp_start[];

// Xbox 360 button pictures, in xbox_buttons.cpp (Ms-PL).
extern const unsigned char xboxButtonChars[5];
extern const unsigned int xboxButtonPixels[5][48 * 48];

namespace {

const char *kTitle = "xDS360";
const char *kVersion = "0.2.2-native-video-dev"; // change when something visible changes
const char *kLogPath = "game:\\xds360.log";
const char *kOldLogPath = "game:\\xds360-anterior.log";
const int kWidth = 1280, kHeight = 720;

// ---------------------------------------------------------------- log

FILE *logFile = 0;

void logLine(const char *format, ...) {
    if (!logFile) return;
    char text[1024];
    _snprintf(text, sizeof(text) - 1, "[%9.3f] ", xds::clockMicroseconds() / 1000000.0);
    text[sizeof(text) - 1] = 0;
    size_t used = strlen(text);
    va_list args;
    va_start(args, format);
    _vsnprintf(text + used, sizeof(text) - used - 2, format, args);
    va_end(args);
    text[sizeof(text) - 2] = 0; // _vsnprintf does not end a cut-off line
    strcat(text, "\n");
    fputs(text, logFile);
    fflush(logFile);
}

void logMemory(const char *when) {
    MEMORYSTATUS memory;
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    logLine("Memory %s: %u MB free of %u MB", when, (unsigned)(memory.dwAvailPhys / 1048576),
            (unsigned)(memory.dwTotalPhys / 1048576));
}

void logOpen() {
    // Keep the previous session's log: it may show the steps before a freeze.
    remove(kOldLogPath);
    const bool kept = rename(kLogPath, kOldLogPath) == 0;
    logFile = fopen(kLogPath, "wc"); // "c": fflush commits to disk
    if (!logFile) logFile = fopen(kLogPath, "w");
    logLine("==== %s %s, built %s %s ====", kTitle, kVersion, __DATE__, __TIME__);
    logLine("Log: %s (previous session %s)", kLogPath,
            kept ? "moved to xds360-anterior.log" : "not found or not moved");
    logMemory("at start");
}

// ---------------------------------------------------------------- video

const char *kVertexShader =
    "struct VS_IN { float4 Position : POSITION; float2 UV : TEXCOORD0; };\n"
    "struct VS_OUT { float4 Position : POSITION; float2 UV : TEXCOORD0; };\n"
    "VS_OUT main(VS_IN In) { VS_OUT Out; Out.Position = In.Position; Out.UV = In.UV; return Out; }\n";

// NooDS pixels are 0xAABBGGRR; stored in an A8R8G8B8 texture, red and blue
// come out swapped, so the shader swaps them back. c0 is the tint color.
const char *kPixelShader =
    "sampler image : register(s0);\n"
    "float4 tint : register(c0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(image, uv).bgra * tint; }\n";

struct QuadVertex {
    float x, y, z, w;
    float u, v;
};

// A texture for ConsoleUI. The Xbox 360 does not wait for the GPU when the CPU
// locks or frees a texture, so each one remembers the frame that last drew it
// and is reused or freed only after that frame's fence has passed.
struct Texture {
    IDirect3DTexture9 *d3d;
    int width, height;
    unsigned usedFrame;
    bool nativeRgb6;
};

IDirect3DDevice9 *device = 0;
IDirect3DVertexShader9 *vertexShader = 0;
IDirect3DPixelShader9 *pixelShader = 0;
IDirect3DPixelShader9 *nativePointShader = 0;
IDirect3DPixelShader9 *nativeLinearShader = 0;
IDirect3DVertexDeclaration9 *vertexDecl = 0;
std::vector<Texture*> freeTextures; // released by ConsoleUI, ready for reuse
unsigned frameNumber = 8;
DWORD frameFences[8];

bool compileShader(const char *source, const char *profile, ID3DXBuffer **code) {
    ID3DXBuffer *errors = 0;
    HRESULT hr = D3DXCompileShader(source, (UINT)strlen(source), NULL, NULL, "main", profile, 0,
                                   code, &errors, NULL);
    if (FAILED(hr)) {
        logLine("Shader %s failed (0x%08X): %s", profile, (unsigned)hr,
                errors ? (const char*)errors->GetBufferPointer() : "");
        if (errors) errors->Release();
        return false;
    }
    if (errors) errors->Release();
    return true;
}

bool videoInit() {
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { logLine("Direct3DCreate9 failed"); return false; }

    D3DPRESENT_PARAMETERS params;
    ZeroMemory(&params, sizeof(params));
    params.BackBufferWidth = kWidth;
    params.BackBufferHeight = kHeight;
    params.BackBufferFormat = D3DFMT_A8R8G8B8;
    params.BackBufferCount = 1;
    params.MultiSampleType = D3DMULTISAMPLE_NONE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    // The emulator runs on its own thread, so the menu thread can wait for vsync.
    params.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &params, &device);
    if (FAILED(hr)) { logLine("CreateDevice failed (0x%08X)", (unsigned)hr); return false; }

    ID3DXBuffer *code = 0;
    if (!compileShader(kVertexShader, "vs_2_0", &code)) return false;
    device->CreateVertexShader((DWORD*)code->GetBufferPointer(), &vertexShader);
    code->Release();
    if (!compileShader(kPixelShader, "ps_2_0", &code)) return false;
    device->CreatePixelShader((DWORD*)code->GetBufferPointer(), &pixelShader);
    code->Release();

    // Optional acceleration: failure leaves the proven CPU conversion active.
    if (compileShader(kNativePointShader, "ps_2_0", &code)) {
        if (FAILED(device->CreatePixelShader((DWORD*)code->GetBufferPointer(), &nativePointShader)))
            nativePointShader = 0;
        code->Release();
    }
    if (compileShader(kNativeLinearShader, "ps_3_0", &code)) {
        if (FAILED(device->CreatePixelShader((DWORD*)code->GetBufferPointer(), &nativeLinearShader)))
            nativeLinearShader = 0;
        code->Release();
    }
    logLine("Native RGB6 shaders: point=%s linear=%s (CPU fallback on failure)",
            nativePointShader ? "ready" : "unavailable", nativeLinearShader ? "ready" : "unavailable");

    static const D3DVERTEXELEMENT9 elements[] = {
        { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    device->CreateVertexDeclaration(elements, &vertexDecl);
    logLine("Video ready: %dx%d", kWidth, kHeight);
    return true;
}

// Waits until the GPU is done with the frame that last drew this texture.
void waitForGpu(Texture *texture) {
    if (frameNumber - texture->usedFrame < 8)
        device->BlockOnFence(frameFences[texture->usedFrame & 7]);
}

// Xbox 360 button pictures (xbox_buttons.cpp, Ms-PL) replace the A, B, X, "+"
// symbols of NooDS's font, and its unused 0x7F cell becomes Back.

void paintXboxButtons(uint8_t *bmp) {
    const int width = U8TO32(bmp, 0x12), height = U8TO32(bmp, 0x16);
    for (int button = 0; button < 5; button++) {
        const int cell = xboxButtonChars[button] - 32;
        const int left = 48 * (cell % 10), top = 48 * (cell / 10);
        for (int y = 0; y < 48; y++) {
            for (int x = 0; x < 48; x++) {
                // BMP rows go bottom-up and each pixel is B, G, R, A.
                uint8_t *p = &bmp[0x46 + (((height - 1 - (top + y)) * width + left + x) << 2)];
                const unsigned int c = xboxButtonPixels[button][y * 48 + x];
                p[0] = c & 0xFF;
                p[1] = (c >> 8) & 0xFF;
                p[2] = (c >> 16) & 0xFF;
                p[3] = c >> 24;
            }
        }
    }
}

// ---------------------------------------------------------------- input

// Extra bits after XInput's 16 button bits, so triggers and stick directions
// can be bound like buttons.
enum {
    kLeftTrigger = 1 << 16, kRightTrigger = 1 << 17,
    kLeftUp = 1 << 18, kLeftDown = 1 << 19, kLeftLeft = 1 << 20, kLeftRight = 1 << 21,
    kRightUp = 1 << 22, kRightDown = 1 << 23, kRightLeft = 1 << 24, kRightRight = 1 << 25
};

XINPUT_GAMEPAD pad;
bool scanned = false;
// Touch cursor on the DS bottom screen, moved with the right stick.
float cursorX = 128, cursorY = 96;
unsigned cursorFrame = 0; // frame in which the game asked for touch input

void scanPad() {
    if (scanned) return;
    XINPUT_STATE state;
    if (XInputGetState(0, &state) == ERROR_SUCCESS) pad = state.Gamepad;
    else ZeroMemory(&pad, sizeof(pad));
    scanned = true;
}

// Right stick moves the touch cursor; RT touches the screen.
MenuTouch xboxTouch() {
    scanPad();
    const int deadZone = 8000;
    int sx = pad.sThumbRX, sy = pad.sThumbRY;
    if (sx > deadZone || sx < -deadZone) cursorX += sx / 32767.0f * 4;
    if (sy > deadZone || sy < -deadZone) cursorY -= sy / 32767.0f * 4;
    cursorX = cursorX < 0 ? 0 : (cursorX > 255 ? 255 : cursorX);
    cursorY = cursorY < 0 ? 0 : (cursorY > 191 ? 191 : cursorY);
    cursorFrame = frameNumber;
    // shortcut: ignores screen rotation, upgrade if rotated layouts need touch
    const ScreenLayout &l = ConsoleUI::layout;
    float x = l.botX + (cursorX + 0.5f) * l.botWidth / 256;
    float y = l.botY + (cursorY + 0.5f) * l.botHeight / 192;
    return MenuTouch(pad.bRightTrigger > 64, x * 720 / kHeight, y * 720 / kHeight);
}

// ---------------------------------------------------------------- audio

IXAudio2SourceVoice *voice = 0;
uint32_t audioBuffers[4][1024];

DWORD WINAPI audioThread(LPVOID) {
    int next = 0;
    for (;;) {
        XAUDIO2_VOICE_STATE state;
        voice->GetState(&state);
        if (state.BuffersQueued >= 3) {
            Sleep(2);
            continue;
        }
        uint32_t *buffer = audioBuffers[next];
        next = (next + 1) & 3;
        ConsoleUI::fillAudioBuffer(buffer, 1024, 32768);
        // NooDS packs (right << 16) | left; big-endian memory then has right
        // first, but XAudio2 wants left first.
        for (int i = 0; i < 1024; i++)
            buffer[i] = (buffer[i] << 16) | (buffer[i] >> 16);
        XAUDIO2_BUFFER submit;
        ZeroMemory(&submit, sizeof(submit));
        submit.AudioBytes = sizeof(audioBuffers[0]);
        submit.pAudioData = (BYTE*)buffer;
        voice->SubmitSourceBuffer(&submit);
    }
}

void audioInit() {
    IXAudio2 *xaudio = 0;
    IXAudio2MasteringVoice *master = 0;
    if (FAILED(XAudio2Create(&xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
        FAILED(xaudio->CreateMasteringVoice(&master))) {
        logLine("XAudio2 could not start: no sound");
        return;
    }
    WAVEFORMATEX format;
    ZeroMemory(&format, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 32768;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 32768 * 4;
    if (FAILED(xaudio->CreateSourceVoice(&voice, &format))) {
        logLine("XAudio2 voice could not start: no sound");
        voice = 0;
        return;
    }
    voice->Start(0);
    HANDLE thread = CreateThread(NULL, 64 * 1024, audioThread, NULL, CREATE_SUSPENDED, NULL);
    XSetThreadProcessor(thread, 1); // shares core 0 with the emulator, but is light
    ResumeThread(thread);
    logLine("Audio ready: 32768 Hz stereo");
}

// ---------------------------------------------------------------- drives

void mountDrive(const char *link, const char *deviceName) {
    KernelString linkName = { (unsigned short)strlen(link), (unsigned short)(strlen(link) + 1), link };
    KernelString devicePath = { (unsigned short)strlen(deviceName), (unsigned short)(strlen(deviceName) + 1),
                                deviceName };
    long status = ObCreateSymbolicLink(&linkName, &devicePath);
    logLine("Mount %s -> %s: %s", link, deviceName, status >= 0 ? "ok" : "not available");
}

// ---------------------------------------------------------------- stats

long long statsStart = 0;
unsigned statsFrames = 0;
unsigned statsTextureUploads = 0;
unsigned statsNativeUploads = 0;
bool wasRunning = false;

void logStats() {
    long long now = xds::clockMicroseconds();
    if (ConsoleUI::running != wasRunning) {
        wasRunning = ConsoleUI::running;
        logLine(wasRunning ? "Game running" : "Game paused or stopped");
        if (wasRunning) logMemory("with the game running");
        statsStart = now;
        statsFrames = 0;
        statsTextureUploads = 0;
        statsNativeUploads = 0;
    }
    statsFrames++;
    if (wasRunning && now - statsStart >= 5000000) {
        logLine("Game fps=%d, screen fps=%.1f, texture uploads=%u native=%u, window ms=%u, frameskip=%d, workers3D=%d",
                ConsoleUI::core ? ConsoleUI::core->fps : 0,
                statsFrames * 1000000.0 / (now - statsStart), statsTextureUploads, statsNativeUploads,
                (unsigned)((now - statsStart) / 1000), Settings::frameskip, Settings::threaded3D);
        statsStart = now;
        statsFrames = 0;
        statsTextureUploads = 0;
        statsNativeUploads = 0;
    }
}

} // namespace

// ---------------------------------------------------------------- ConsoleUI

uint32_t ConsoleUI::defaultKeys[] = {
    XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
    XINPUT_GAMEPAD_DPAD_RIGHT | kLeftRight, XINPUT_GAMEPAD_DPAD_LEFT | kLeftLeft,
    XINPUT_GAMEPAD_DPAD_UP | kLeftUp, XINPUT_GAMEPAD_DPAD_DOWN | kLeftDown,
    XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
    XINPUT_GAMEPAD_RIGHT_THUMB, // menu: right stick click
    kLeftTrigger,               // fast-forward while held
    0,                          // fast-forward toggle: unbound
    XINPUT_GAMEPAD_LEFT_THUMB   // swap screens: left stick click
};

const char *ConsoleUI::keyNames[] = {
    "Up", "Down", "Left", "Right", "Start", "Back", "LS Click", "RS Click",
    "LB", "RB", "", "", "A", "B", "X", "Y",
    "LT", "RT", "LS Up", "LS Down", "LS Left", "LS Right", "RS Up", "RS Down",
    "RS Left", "RS Right", "", "", "", "", "", ""
};

void ConsoleUI::startFrame(uint32_t color) {
    // ConsoleUI colors are 0xAABBGGRR; Clear takes 0xAARRGGBB.
    device->Clear(0, NULL, D3DCLEAR_TARGET,
                  (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color >> 16) & 0xFF), 1.0f, 0);
    device->BeginScene();
    device->SetRenderState(D3DRS_VIEWPORTENABLE, FALSE); // positions are in pixels
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetVertexDeclaration(vertexDecl);
    device->SetVertexShader(vertexShader);
    device->SetPixelShader(pixelShader);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}

void ConsoleUI::endFrame() {
    // Touch cursor: a small cross, only while the game is asking for touch.
    if (cursorFrame == frameNumber) {
        const ScreenLayout &l = ConsoleUI::layout;
        float x = l.botX + (cursorX + 0.5f) * l.botWidth / 256;
        float y = l.botY + (cursorY + 0.5f) * l.botHeight / 192;
        drawRectangle(x - 7, y - 1, 15, 3, 0xFF000000);
        drawRectangle(x - 1, y - 7, 3, 15, 0xFF000000);
        drawRectangle(x - 6, y, 13, 1, 0xFFFFFFFF);
        drawRectangle(x, y - 6, 1, 13, 0xFFFFFFFF);
    }
    device->EndScene();
    frameFences[frameNumber & 7] = device->InsertFence();
    device->Present(NULL, NULL, NULL, NULL);
    frameNumber++;
    scanned = false;
    logStats();
}

int ConsoleUI::gpuNativeVideo = 1;

bool ConsoleUI::useNativeVideo() {
    // Match getFrame()'s supported native geometry; keep temporal blending,
    // GBA and high-resolution/filter output on the CPU path.
    return gpuNativeVideo && core && !core->gbaMode && !Settings::screenGhost &&
        !Settings::highRes3D && Settings::screenFilter != 1 &&
        (Settings::screenFilter ? nativeLinearShader != 0 : nativePointShader != 0);
}

void *ConsoleUI::createTexture(uint32_t *data, int width, int height, bool nativeRgb6) {
    // Reuse a released texture of the same size (the game screens are
    // recreated every frame), or make a new one.
    Texture *texture = 0;
    for (size_t i = 0; i < freeTextures.size(); i++) {
        // Not one drawn in this frame: its fence does not exist yet.
        if (freeTextures[i]->width == width && freeTextures[i]->height == height &&
            freeTextures[i]->usedFrame != frameNumber) {
            texture = freeTextures[i];
            freeTextures.erase(freeTextures.begin() + i);
            waitForGpu(texture);
            break;
        }
    }
    if (!texture) {
        texture = new Texture;
        texture->width = width;
        texture->height = height;
        texture->usedFrame = 0;
        texture->nativeRgb6 = nativeRgb6;
        if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT,
                                         &texture->d3d, NULL))) {
            logLine("CreateTexture %dx%d failed", width, height);
            texture->d3d = 0;
            return texture;
        }
    }
    texture->nativeRgb6 = nativeRgb6;
    device->SetTexture(0, NULL);
    D3DLOCKED_RECT locked;
    if (texture->d3d && SUCCEEDED(texture->d3d->LockRect(0, &locked, NULL, 0))) {
        for (int y = 0; y < height; y++)
            memcpy((BYTE*)locked.pBits + y * locked.Pitch, &data[y * width], width * 4);
        texture->d3d->UnlockRect(0);
        statsTextureUploads++;
        if (nativeRgb6) statsNativeUploads++;
    }
    return texture;
}

void ConsoleUI::destroyTexture(void *pointer) {
    Texture *texture = (Texture*)pointer;
    if (freeTextures.size() < 16) {
        freeTextures.push_back(texture);
        return;
    }
    waitForGpu(texture); // never free what the GPU may still read
    if (texture->d3d) texture->d3d->Release();
    delete texture;
}

void ConsoleUI::drawTexture(void *pointer, float tx, float ty, float tw, float th,
    float x, float y, float w, float h, bool filter, int rotation, uint32_t color) {
    Texture *texture = (Texture*)pointer;
    if (!texture->d3d) return;
    float s1 = tx / texture->width, t1 = ty / texture->height;
    float s2 = (tx + tw) / texture->width, t2 = (ty + th) / texture->height;
    // Texture coordinates of the corners top-left, top-right, bottom-right,
    // bottom-left for each rotation (as in NooDS's main_wiiu.cpp).
    const float corners[3][8] = {
        { s1, t1, s2, t1, s2, t2, s1, t2 }, // none
        { s1, t2, s1, t1, s2, t1, s2, t2 }, // clockwise
        { s2, t1, s2, t2, s1, t2, s1, t1 }  // counter-clockwise
    };
    const float *c = corners[rotation < 0 || rotation > 2 ? 0 : rotation];
    x -= 0.5f;
    y -= 0.5f;
    QuadVertex quad[4] = { // triangle strip: top-left, top-right, bottom-left, bottom-right
        { x, y, 0.0f, 1.0f, c[0], c[1] },
        { x + w, y, 0.0f, 1.0f, c[2], c[3] },
        { x, y + h, 0.0f, 1.0f, c[6], c[7] },
        { x + w, y + h, 0.0f, 1.0f, c[4], c[5] }
    };
    const float tint[4] = { (color & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f,
                            ((color >> 16) & 0xFF) / 255.0f, (color >> 24) / 255.0f };
    device->SetPixelShaderConstantF(0, tint, 1);
    if (texture->nativeRgb6) {
        // Never interpolate packed RGB6 bytes. The linear shader decodes four
        // point samples and only then blends the expanded colours.
        device->SetPixelShader(filter ? nativeLinearShader : nativePointShader);
        const float imageSize[4] = { (float)texture->width, (float)texture->height,
            1.0f / texture->width, 1.0f / texture->height };
        device->SetPixelShaderConstantF(1, imageSize, 1);
    }
    else device->SetPixelShader(pixelShader);
    const bool hardwareFilter = filter && !texture->nativeRgb6;
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, hardwareFilter ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, hardwareFilter ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    device->SetTexture(0, texture->d3d);
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(QuadVertex));
    texture->usedFrame = frameNumber;
}

uint32_t ConsoleUI::getInputHeld() {
    scanPad();
    const int stick = 16000;
    uint32_t held = pad.wButtons;
    if (pad.bLeftTrigger > 64) held |= kLeftTrigger;
    if (pad.bRightTrigger > 64) held |= kRightTrigger;
    if (pad.sThumbLY > stick) held |= kLeftUp;
    if (pad.sThumbLY < -stick) held |= kLeftDown;
    if (pad.sThumbLX < -stick) held |= kLeftLeft;
    if (pad.sThumbLX > stick) held |= kLeftRight;
    if (pad.sThumbRY > stick) held |= kRightUp;
    if (pad.sThumbRY < -stick) held |= kRightDown;
    if (pad.sThumbRX < -stick) held |= kRightLeft;
    if (pad.sThumbRX > stick) held |= kRightRight;
    return held;
}

MenuTouch ConsoleUI::getInputTouch() {
    return MenuTouch(false, 0, 0); // no touch screen; the game uses xboxTouch
}

// ---------------------------------------------------------------- main

void __cdecl main() {
    XSetThreadProcessor(GetCurrentThread(), 5); // menus and drawing
    logOpen();
    if (!videoInit()) {
        XLaunchNewImage("", 0);
        return;
    }
    mountDrive("\\??\\hdd:", "\\Device\\Harddisk0\\Partition1");
    mountDrive("\\??\\usb0:", "\\Device\\Mass0");
    mountDrive("\\??\\usb1:", "\\Device\\Mass1");
    mountDrive("\\??\\usb2:", "\\Device\\Mass2");
    mkdir("/game:/roms"); // NooDS makes saves, states and cheats itself

    // Hardware threads, in the order NooDS starts its threads: emulator 0,
    // then saves on 1 and the default two 3D workers on 2 and 4.
    // Xenon SMT siblings are 0/1, 2/3 and 4/5: spread 3D across physical
    // cores rather than putting both workers on siblings 2/3. Menus use 5.
    static const int processors[5] = { 0, 1, 2, 4, 3 };
    xds::setThreadProcessors(processors, 5);
    logLine("Worker processors: core=0 save=1 render=2,4 extra=3; frontend=5 audio=1");

    // Defaults for a first run (noods.ini overrides them afterwards).
    // The settings menu only knows 0-2 threads; 2D threads are created every frame, too costly here.
    Settings::threaded3D = 2;
    Settings::threaded2D = 0;
    Settings::savesFolder = 1;

    paintXboxButtons(_binary_src_ui_console_images_font_bmp_start);
    audioInit();
    ConsoleUI::initialize(kWidth, kHeight, "/game:/roms", "/game:");
    if (Settings::threaded3D > 2) Settings::threaded3D = 2; // an old noods.ini would crash the menu
    logLine("Settings: threaded3D=%d fpsLimiter=%d savesFolder=%d gpuNativeVideo=%d", Settings::threaded3D,
            Settings::fpsLimiter, Settings::savesFolder, ConsoleUI::gpuNativeVideo);
    ConsoleUI::fileBrowser();
    ConsoleUI::mainLoop(xboxTouch);

    logLine("Leaving to the dashboard");
    if (logFile) fclose(logFile);
    XLaunchNewImage("", 0);
}
