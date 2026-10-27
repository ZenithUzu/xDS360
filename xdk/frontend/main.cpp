// SPDX-License-Identifier: GPL-3.0-or-later
// xDS360 minimal frontend for the Xbox 360 XDK: finds one ROM, runs it, shows
// both DS screens stacked in 720p, reads the first controller, writes a log.
#define NOMINMAX
#include <xtl.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "emu.h"
#include "font8x8_basic.h"
#include "xds_platform.h"

namespace {

const char *kTitle = "xDS360 dev";
const char *kLogPath = "game:\\xds360.log";
const char *kOldLogPath = "game:\\xds360-anterior.log";

// DS refresh rate is 59.8261 Hz.
const long long kFrameMicroseconds = 16715;
const long long kSaveMicroseconds = 5000000;
const long long kStallMicroseconds = 10000000;
const long long kExitHoldMicroseconds = 1500000;

// Screen placement in the 1280x720 back buffer: 256x384 scaled to 480x720.
const float kScreenX = 400.0f, kScreenY = 0.0f, kScreenW = 480.0f, kScreenH = 720.0f;

// Text panel: 24 columns x 10 rows of 8x8 characters, drawn at 2x.
const int kTextCols = 24, kTextRows = 10;
const int kTextW = kTextCols * 8, kTextH = kTextRows * 8;

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

void logOpen() {
    // Keep the previous session's log: it may show the steps before a freeze.
    remove(kOldLogPath);
    rename(kLogPath, kOldLogPath);
    logFile = fopen(kLogPath, "wc"); // "c": fflush commits to disk
    if (!logFile) logFile = fopen(kLogPath, "w");
    logLine("%s (built %s %s)", kTitle, __DATE__, __TIME__);
}

// ---------------------------------------------------------------- video

const char *kVertexShader =
    "struct VS_IN { float4 Position : POSITION; float2 UV : TEXCOORD0; };\n"
    "struct VS_OUT { float4 Position : POSITION; float2 UV : TEXCOORD0; };\n"
    "VS_OUT main(VS_IN In) { VS_OUT Out; Out.Position = In.Position; Out.UV = In.UV; return Out; }\n";

const char *kPixelShader =
    "sampler image : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(image, uv); }\n";

struct Vertex {
    float x, y, z, w;
    float u, v;
};

IDirect3DDevice9 *device = 0;
IDirect3DVertexShader9 *vertexShader = 0;
IDirect3DPixelShader9 *pixelShader = 0;
IDirect3DVertexDeclaration9 *vertexDecl = 0;
// The Xbox 360 does not wait for the GPU when the CPU locks a texture, so each
// texture is rewritten only after the fence set when it was last drawn has
// passed (same approach as AdvanXe360's UI renderer).
IDirect3DTexture9 *screenTextures[2] = { 0, 0 };
DWORD screenFences[2] = { 0, 0 };
int screenIndex = 0;
IDirect3DTexture9 *textTexture = 0;
DWORD textFence = 0;
DWORD colorTable[32768]; // BGR555 -> A8R8G8B8

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
    params.BackBufferWidth = 1280;
    params.BackBufferHeight = 720;
    params.BackBufferFormat = D3DFMT_A8R8G8B8;
    params.BackBufferCount = 1;
    params.MultiSampleType = D3DMULTISAMPLE_NONE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    // No vsync wait: the emulator paces itself and must not block on Present.
    params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
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

    static const D3DVERTEXELEMENT9 elements[] = {
        { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    device->CreateVertexDeclaration(elements, &vertexDecl);

    for (int i = 0; i < 2; i++) {
        hr = device->CreateTexture(emu::kWidth, emu::kHeight, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT,
                                   &screenTextures[i], NULL);
        if (FAILED(hr)) { logLine("Screen texture failed (0x%08X)", (unsigned)hr); return false; }
    }
    hr = device->CreateTexture(kTextW, kTextH, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, &textTexture, NULL);
    if (FAILED(hr)) { logLine("Text texture failed (0x%08X)", (unsigned)hr); return false; }

    for (int i = 0; i < 32768; i++) {
        DWORD r = i & 0x1F, g = (i >> 5) & 0x1F, b = (i >> 10) & 0x1F;
        r = (r << 3) | (r >> 2);
        g = (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);
        colorTable[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    logLine("Video ready: 1280x720");
    return true;
}

// Converts a DS frame into the next screen texture. The two textures take
// turns, so the CPU never writes one the GPU may still be drawing.
void uploadScreens(const uint16_t *pixels, int cursorX, int cursorY) {
    screenIndex ^= 1;
    IDirect3DTexture9 *texture = screenTextures[screenIndex];
    device->SetTexture(0, NULL);
    if (screenFences[screenIndex]) device->BlockOnFence(screenFences[screenIndex]);
    D3DLOCKED_RECT locked;
    if (FAILED(texture->LockRect(0, &locked, NULL, 0))) return;
    for (int y = 0; y < emu::kHeight; y++) {
        DWORD *row = (DWORD*)((BYTE*)locked.pBits + y * locked.Pitch);
        const uint16_t *source = pixels + y * emu::kWidth;
        for (int x = 0; x < emu::kWidth; x++)
            row[x] = colorTable[source[x] & 0x7FFF];
    }
    // Touch cursor: a small cross on the bottom screen (not part of the game).
    for (int d = -3; d <= 3; d++) {
        int x = cursorX + d, y = cursorY;
        if (x >= 0 && x < emu::kWidth)
            ((DWORD*)((BYTE*)locked.pBits + (192 + y) * locked.Pitch))[x] ^= 0x00FFFFFF;
        x = cursorX;
        y = cursorY + d;
        if (d != 0 && y >= 0 && y < 192)
            ((DWORD*)((BYTE*)locked.pBits + (192 + y) * locked.Pitch))[x] ^= 0x00FFFFFF;
    }
    texture->UnlockRect(0);
}

// Draws up to kTextRows lines of text into the text panel.
void setText(const std::vector<std::string> &lines) {
    device->SetTexture(0, NULL);
    if (textFence) device->BlockOnFence(textFence);
    D3DLOCKED_RECT locked;
    if (FAILED(textTexture->LockRect(0, &locked, NULL, 0))) return;
    for (int row = 0; row < kTextRows; row++) {
        const std::string empty;
        const std::string &line = row < (int)lines.size() ? lines[row] : empty;
        for (int py = 0; py < 8; py++) {
            DWORD *out = (DWORD*)((BYTE*)locked.pBits + (row * 8 + py) * locked.Pitch);
            for (int col = 0; col < kTextCols; col++) {
                unsigned char c = col < (int)line.size() ? (unsigned char)line[col] : ' ';
                unsigned char bits = font8x8_basic[c & 0x7F][py];
                for (int px = 0; px < 8; px++)
                    out[col * 8 + px] = (bits & (1 << px)) ? 0xFFFFFFFF : 0xFF000000;
            }
        }
    }
    textTexture->UnlockRect(0);
}

// Draws a texture in pixel coordinates and returns the fence for it.
DWORD drawQuad(IDirect3DTexture9 *texture, float x, float y, float w, float h) {
    x -= 0.5f;
    y -= 0.5f;
    Vertex quad[4] = {
        { x, y, 0.0f, 1.0f, 0.0f, 0.0f },
        { x + w, y, 0.0f, 1.0f, 1.0f, 0.0f },
        { x, y + h, 0.0f, 1.0f, 0.0f, 1.0f },
        { x + w, y + h, 0.0f, 1.0f, 1.0f, 1.0f }
    };
    device->SetTexture(0, texture);
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
    return device->InsertFence();
}

void present(bool showScreens) {
    device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    device->BeginScene();
    device->SetRenderState(D3DRS_VIEWPORTENABLE, FALSE); // positions are in pixels
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetVertexDeclaration(vertexDecl);
    device->SetVertexShader(vertexShader);
    device->SetPixelShader(pixelShader);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    if (showScreens)
        screenFences[screenIndex] = drawQuad(screenTextures[screenIndex], kScreenX, kScreenY, kScreenW, kScreenH);
    textFence = drawQuad(textTexture, 16.0f, 16.0f, kTextW * 2.0f, kTextH * 2.0f);
    device->EndScene();
    device->Present(NULL, NULL, NULL, NULL);
}

// ---------------------------------------------------------------- input

struct Pad {
    WORD buttons;
    BYTE rightTrigger;
    SHORT rightX, rightY;
};

Pad readPad() {
    Pad pad = { 0, 0, 0, 0 };
    XINPUT_STATE state;
    if (XInputGetState(0, &state) == ERROR_SUCCESS) {
        pad.buttons = state.Gamepad.wButtons;
        pad.rightTrigger = state.Gamepad.bRightTrigger;
        pad.rightX = state.Gamepad.sThumbRX;
        pad.rightY = state.Gamepad.sThumbRY;
    }
    return pad;
}

uint16_t dsButtons(const Pad &pad) {
    static const struct { WORD xbox; uint16_t ds; } map[] = {
        { XINPUT_GAMEPAD_A, emu::kA }, { XINPUT_GAMEPAD_B, emu::kB },
        { XINPUT_GAMEPAD_X, emu::kX }, { XINPUT_GAMEPAD_Y, emu::kY },
        { XINPUT_GAMEPAD_LEFT_SHOULDER, emu::kL }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, emu::kR },
        { XINPUT_GAMEPAD_START, emu::kStart }, { XINPUT_GAMEPAD_BACK, emu::kSelect },
        { XINPUT_GAMEPAD_DPAD_UP, emu::kUp }, { XINPUT_GAMEPAD_DPAD_DOWN, emu::kDown },
        { XINPUT_GAMEPAD_DPAD_LEFT, emu::kLeft }, { XINPUT_GAMEPAD_DPAD_RIGHT, emu::kRight }
    };
    uint16_t buttons = 0;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (pad.buttons & map[i].xbox) buttons |= map[i].ds;
    return buttons;
}

// Right stick moves the touch cursor (up to 6 pixels per DS frame).
int stickStep(SHORT value) {
    const int deadZone = 8000;
    int magnitude = value < 0 ? -(int)value : (int)value;
    if (magnitude <= deadZone) return 0;
    int step = 1 + (magnitude - deadZone) * 5 / (32767 - deadZone);
    return value < 0 ? -step : step;
}

// ---------------------------------------------------------------- ROM

struct Rom {
    std::string path;
    std::string name;
    unsigned long long size;
};

void findRoms(const char *folder, std::vector<Rom> *roms) {
    std::string pattern = std::string(folder) + "*.nds";
    WIN32_FIND_DATA data;
    HANDLE find = FindFirstFile(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        Rom rom;
        rom.name = data.cFileName;
        rom.path = std::string(folder) + data.cFileName;
        rom.size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        roms->push_back(rom);
    } while (FindNextFile(find, &data));
    FindClose(find);
}

bool byName(const Rom &a, const Rom &b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; }

// ---------------------------------------------------------------- screens

std::string shortText(const std::string &text) {
    return text.size() <= (size_t)kTextCols ? text : text.substr(0, kTextCols);
}

// Shows a message until B is pressed (or forever if video failed).
void showMessage(const std::vector<std::string> &lines) {
    std::vector<std::string> wrapped;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = lines[i];
        do {
            wrapped.push_back(line.substr(0, kTextCols));
            line = line.size() > (size_t)kTextCols ? line.substr(kTextCols) : "";
        } while (!line.empty());
    }
    wrapped.push_back("");
    wrapped.push_back("B: exit");
    setText(wrapped);
    bool released = false; // B may still be held from the game
    for (;;) {
        present(false);
        const bool pressed = (readPad().buttons & XINPUT_GAMEPAD_B) != 0;
        if (pressed && released) return;
        if (!pressed) released = true;
        Sleep(16);
    }
}

void quit() {
    logLine("Leaving to the dashboard");
    if (logFile) fclose(logFile);
    XLaunchNewImage("", 0);
}

} // namespace

void __cdecl main() {
    XSetThreadProcessor(GetCurrentThread(), 0); // emulator on core 0
    logOpen();
    if (!videoInit()) { quit(); return; }

    std::vector<Rom> roms;
    findRoms("game:\\roms\\", &roms);
    if (roms.empty()) findRoms("game:\\", &roms);
    std::sort(roms.begin(), roms.end(), byName);
    logLine("ROMs found: %u", (unsigned)roms.size());
    for (size_t i = 0; i < roms.size() && i < 20; i++)
        logLine("  %s (%llu bytes)", roms[i].path.c_str(), roms[i].size);
    if (roms.empty()) {
        std::vector<std::string> lines;
        lines.push_back(kTitle);
        lines.push_back("");
        lines.push_back("No .nds file found.");
        lines.push_back("Copy one to game:\\roms\\");
        lines.push_back("(name up to 42 chars)");
        showMessage(lines);
        quit();
        return;
    }

    const Rom &rom = roms[0];
    // Small ROMs are read into memory once; big ones (Pokemon B/W is 256MB)
    // are read from the file as the game asks for them.
    const bool romInRam = rom.size <= 128ULL * 1024 * 1024;
    static const int renderProcessors[3] = { 2, 3, 4 }; // 3D workers on cores 1 and 2
    xds::setThreadProcessors(renderProcessors, 3);

    emu::Options options;
    options.romPath = rom.path;
    options.biosFolder = "game:";
    options.romInRam = romInRam;
    options.renderThreads = 3;
    logLine("Starting %s (romInRam=%d, 3D threads=3)", rom.path.c_str(), romInRam ? 1 : 0);

    std::vector<std::string> lines;
    lines.push_back(kTitle);
    lines.push_back("");
    lines.push_back("Loading...");
    lines.push_back(shortText(rom.name));
    setText(lines);
    present(false);

    std::string error;
    long long startTime = xds::clockMicroseconds();
    if (!emu::start(options, &error)) {
        logLine("Start failed: %s", error.c_str());
        lines.clear();
        lines.push_back(kTitle);
        lines.push_back("");
        lines.push_back(error);
        lines.push_back(shortText(rom.name));
        showMessage(lines);
        quit();
        return;
    }
    logLine("Core ready in %.1f s", (xds::clockMicroseconds() - startTime) / 1000000.0);

    static uint16_t frame[emu::kWidth * emu::kHeight];
    int cursorX = 128, cursorY = 96;
    uint32_t lastCompleted = emu::completedFrames();
    long long now = xds::clockMicroseconds();
    long long nextFrameTime = now, lastSave = now, lastProgress = now, exitHeldSince = 0;
    long long statsStart = now, statsCore = 0, logStatsAt = now + 5000000;
    unsigned statsFrames = 0, statsVideo = 0;
    bool haveFrame = false;

    for (;;) {
        long long before = xds::clockMicroseconds();
        if (!emu::run(&error)) {
            logLine("Core stopped: %s | %s", error.c_str(), emu::diagnostics().c_str());
            emu::writeSave();
            lines.clear();
            lines.push_back(kTitle);
            lines.push_back("");
            lines.push_back(error);
            showMessage(lines);
            break;
        }
        now = xds::clockMicroseconds();
        statsCore += now - before;

        const uint32_t completed = emu::completedFrames();
        if (completed == lastCompleted) {
            // runCore also returns when an emulated CPU halts or resumes.
            if (now - lastProgress > kStallMicroseconds || emu::unknownOpcodes() >= 4096) {
                logLine("No progress: %s", emu::diagnostics().c_str());
                emu::writeSave();
                lines.clear();
                lines.push_back(kTitle);
                lines.push_back("");
                lines.push_back("The game stopped running.");
                lines.push_back("See xds360.log");
                showMessage(lines);
                break;
            }
            continue;
        }
        statsFrames += completed - lastCompleted;
        lastCompleted = completed;
        lastProgress = now;

        // Input, once per DS frame.
        Pad pad = readPad();
        const WORD exitCombo = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START;
        if ((pad.buttons & exitCombo) == exitCombo) {
            if (!exitHeldSince) exitHeldSince = now;
            else if (now - exitHeldSince >= kExitHoldMicroseconds) break;
        } else {
            exitHeldSince = 0;
        }
        cursorX = std::max(0, std::min(255, cursorX + stickStep(pad.rightX)));
        cursorY = std::max(0, std::min(191, cursorY - stickStep(pad.rightY))); // stick up = screen up
        emu::setInput(dsButtons(pad), pad.rightTrigger > 32, cursorX, cursorY);

        if (emu::takeFrame(frame)) {
            haveFrame = true;
            uploadScreens(frame, cursorX, cursorY);
            statsVideo++;
        }

        // Once a second: update the on-screen numbers.
        if (now - statsStart >= 1000000) {
            double seconds = (now - statsStart) / 1000000.0;
            double fps = statsFrames / seconds;
            char text[64];
            lines.clear();
            lines.push_back(kTitle);
            _snprintf(text, sizeof(text) - 1, "FPS   %5.1f", fps);
            text[sizeof(text) - 1] = 0;
            lines.push_back(text);
            _snprintf(text, sizeof(text) - 1, "SPEED %5.0f%%", fps * 100.0 / 59.8261);
            lines.push_back(text);
            _snprintf(text, sizeof(text) - 1, "CORE  %5.1f ms",
                      statsFrames ? statsCore / 1000.0 / statsFrames : 0.0);
            lines.push_back(text);
            lines.push_back("");
            lines.push_back(shortText(rom.name));
            lines.push_back("RT: touch  R-stick: move");
            lines.push_back("Back+Start: exit");
            setText(lines);
            if (now >= logStatsAt) {
                logLine("fps=%.1f video=%.1f core=%.1fms/frame", fps, statsVideo / seconds,
                        statsFrames ? statsCore / 1000.0 / statsFrames : 0.0);
                logStatsAt = now + 5000000;
            }
            statsStart = now;
            statsCore = 0;
            statsFrames = 0;
            statsVideo = 0;
        }

        present(haveFrame);

        if (now - lastSave >= kSaveMicroseconds) {
            emu::writeSave();
            lastSave = now;
        }

        // Keep the DS at 59.83 frames per second when the console is fast enough.
        nextFrameTime += kFrameMicroseconds;
        now = xds::clockMicroseconds();
        if (nextFrameTime < now - 50000) nextFrameTime = now; // too far behind: don't rush
        while (now < nextFrameTime) {
            if (nextFrameTime - now > 2000) Sleep(1);
            now = xds::clockMicroseconds();
        }
    }

    emu::stop();
    quit();
}
