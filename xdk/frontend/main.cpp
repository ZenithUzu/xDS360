// SPDX-License-Identifier: GPL-3.0-or-later
// xDS360 frontend for the Xbox 360 XDK: a simple file browser to pick a ROM,
// then both DS screens stacked in 720p, the first controller, and a log.
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

// Kernel call that makes a device reachable by a name, such as the hard drive
// as "hdd:". Same layout as the kernel's ANSI_STRING.
struct KernelString {
    unsigned short length;
    unsigned short maximumLength;
    const char *buffer;
};
extern "C" long ObCreateSymbolicLink(KernelString *link, KernelString *device);

namespace {

const char *kTitle = "xDS360 dev";
const char *kLogPath = "game:\\xds360.log";
const char *kOldLogPath = "game:\\xds360-anterior.log";
const char *kConfigPath = "game:\\xds360.cfg";
const char *kRomsFolder = "game:\\roms\\";

// DS refresh rate is 59.8261 Hz.
const long long kFrameMicroseconds = 16715;
const long long kSaveMicroseconds = 5000000;
const long long kStallMicroseconds = 10000000;
const long long kExitHoldMicroseconds = 1500000;

// Screen placement in the 1280x720 back buffer: 256x384 scaled to 480x720.
const float kScreenX = 400.0f, kScreenY = 0.0f, kScreenW = 480.0f, kScreenH = 720.0f;

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

// Each text row is 8 pixels of letters plus 2 of space, drawn at 2x.
const int kRowHeight = 10;

// A block of 8x8 text drawn at 2x. The Xbox 360 does not wait for the GPU
// when the CPU locks a texture, so a texture is rewritten only after the fence
// set when it was last drawn has passed (as in AdvanXe360's UI renderer).
struct TextPanel {
    IDirect3DTexture9 *texture;
    DWORD fence;
    int cols, rows;
    float x, y;
};

IDirect3DDevice9 *device = 0;
IDirect3DVertexShader9 *vertexShader = 0;
IDirect3DPixelShader9 *pixelShader = 0;
IDirect3DVertexDeclaration9 *vertexDecl = 0;
IDirect3DTexture9 *screenTextures[2] = { 0, 0 };
DWORD screenFences[2] = { 0, 0 };
int screenIndex = 0;
TextPanel statusPanel = { 0, 0, 24, 10, 16.0f, 16.0f };  // in game, left of the screens
TextPanel menuPanel = { 0, 0, 76, 32, 32.0f, 40.0f };    // browser and messages
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

bool createTexture(int width, int height, IDirect3DTexture9 **texture) {
    HRESULT hr = device->CreateTexture(width, height, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT,
                                       texture, NULL);
    if (FAILED(hr)) logLine("CreateTexture %dx%d failed (0x%08X)", width, height, (unsigned)hr);
    return SUCCEEDED(hr);
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

    if (!createTexture(emu::kWidth, emu::kHeight, &screenTextures[0]) ||
        !createTexture(emu::kWidth, emu::kHeight, &screenTextures[1]) ||
        !createTexture(statusPanel.cols * 8, statusPanel.rows * kRowHeight, &statusPanel.texture) ||
        !createTexture(menuPanel.cols * 8, menuPanel.rows * kRowHeight, &menuPanel.texture))
        return false;

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

// Converts a DS frame into the next screen texture (the two take turns).
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

// Writes lines of text into a panel. Row `highlight` is drawn inverted.
void setText(TextPanel &panel, const std::vector<std::string> &lines, int highlight = -1) {
    device->SetTexture(0, NULL);
    if (panel.fence) device->BlockOnFence(panel.fence);
    D3DLOCKED_RECT locked;
    if (FAILED(panel.texture->LockRect(0, &locked, NULL, 0))) return;
    const std::string empty;
    for (int row = 0; row < panel.rows; row++) {
        const std::string &line = row < (int)lines.size() ? lines[row] : empty;
        const DWORD ink = row == highlight ? 0xFF000000 : 0xFFFFFFFF;
        const DWORD paper = row == highlight ? 0xFFFFFFFF : 0xFF000000;
        for (int py = 0; py < kRowHeight; py++) {
            DWORD *out = (DWORD*)((BYTE*)locked.pBits + (row * kRowHeight + py) * locked.Pitch);
            for (int col = 0; col < panel.cols; col++) {
                unsigned char c = col < (int)line.size() ? (unsigned char)line[col] : ' ';
                if (c >= 128) c = '?'; // the font has no accents (UTF-8 bytes from FTP)
                unsigned char bits = py < 8 ? font8x8_basic[c][py] : 0;
                for (int px = 0; px < 8; px++)
                    out[col * 8 + px] = (bits & (1 << px)) ? ink : paper;
            }
        }
    }
    panel.texture->UnlockRect(0);
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

void present(bool showScreens, TextPanel &panel) {
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
    panel.fence = drawQuad(panel.texture, panel.x, panel.y, panel.cols * 16.0f, panel.rows * kRowHeight * 2.0f);
    device->EndScene();
    device->Present(NULL, NULL, NULL, NULL);
}

// ---------------------------------------------------------------- input

struct Pad {
    WORD buttons;
    BYTE rightTrigger;
    SHORT leftY, rightX, rightY;
};

Pad readPad() {
    Pad pad = { 0, 0, 0, 0, 0 };
    XINPUT_STATE state;
    if (XInputGetState(0, &state) == ERROR_SUCCESS) {
        pad.buttons = state.Gamepad.wButtons;
        pad.rightTrigger = state.Gamepad.bRightTrigger;
        pad.leftY = state.Gamepad.sThumbLY;
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

// Waits until no button is held, so a press does not leak into the next screen.
void waitForRelease(TextPanel &panel) {
    for (int i = 0; i < 180 && readPad().buttons != 0; i++) {
        present(false, panel);
        Sleep(16);
    }
}

// ---------------------------------------------------------------- files

void mountDrive(const char *link, const char *deviceName) {
    KernelString linkName = { (unsigned short)strlen(link), (unsigned short)(strlen(link) + 1), link };
    KernelString devicePath = { (unsigned short)strlen(deviceName), (unsigned short)(strlen(deviceName) + 1),
                                deviceName };
    long status = ObCreateSymbolicLink(&linkName, &devicePath);
    logLine("Mount %s -> %s: %s", link, deviceName, status >= 0 ? "ok" : "not available");
}

struct Drive {
    const char *path;
    const char *label;
};
const Drive kDrives[] = {
    { "game:\\", "Emulator folder" },
    { "hdd:\\", "Hard drive" },
    { "usb0:\\", "USB 1" },
    { "usb1:\\", "USB 2" },
    { "usb2:\\", "USB 3" }
};

void mountDrives() {
    mountDrive("\\??\\hdd:", "\\Device\\Harddisk0\\Partition1");
    mountDrive("\\??\\usb0:", "\\Device\\Mass0");
    mountDrive("\\??\\usb1:", "\\Device\\Mass1");
    mountDrive("\\??\\usb2:", "\\Device\\Mass2");
}

// The folders the emulator uses, next to default.xex.
void createFolders() {
    static const char *folders[] = { "game:\\roms", "game:\\saves", "game:\\bios" };
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        if (CreateDirectory(folders[i], NULL))
            logLine("Created %s", folders[i]);
    }
}

bool folderExists(const std::string &path) {
    DWORD attributes = GetFileAttributes(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool hasNdsExtension(const char *name) {
    size_t length = strlen(name);
    return length > 4 && _stricmp(name + length - 4, ".nds") == 0;
}

std::string loadLastFolder() {
    std::string folder;
    FILE *file = fopen(kConfigPath, "r");
    if (!file) return folder;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "lastFolder=", 11) != 0) continue;
        folder = line + 11;
        while (!folder.empty() && (folder[folder.size() - 1] == '\n' || folder[folder.size() - 1] == '\r'))
            folder.erase(folder.size() - 1);
    }
    fclose(file);
    return folder;
}

void saveLastFolder(const std::string &folder) {
    FILE *file = fopen(kConfigPath, "w");
    if (!file) return;
    fprintf(file, "lastFolder=%s\n", folder.c_str());
    fclose(file);
}

// ---------------------------------------------------------------- browser

struct Entry {
    std::string name;
    bool folder;
    unsigned long long size;
};

bool entryOrder(const Entry &a, const Entry &b) {
    if (a.folder != b.folder) return a.folder; // folders first
    return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
}

// Folders and .nds files in `folder` ("hdd:\Games\"), sorted.
std::vector<Entry> listFolder(const std::string &folder) {
    std::vector<Entry> entries;
    WIN32_FIND_DATA data;
    HANDLE find = FindFirstFile((folder + "*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return entries;
    do {
        const bool isFolder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, "..")) continue;
        if (!isFolder && !hasNdsExtension(data.cFileName)) continue;
        Entry entry;
        entry.name = data.cFileName;
        entry.folder = isFolder;
        entry.size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        entries.push_back(entry);
    } while (entries.size() < 5000 && FindNextFile(find, &data));
    FindClose(find);
    std::sort(entries.begin(), entries.end(), entryOrder);
    return entries;
}

// "hdd:\Games\DS\" -> "hdd:\Games\". A drive root goes back to the drive list ("").
std::string parentFolder(const std::string &folder) {
    // Folders always end in a backslash; a drive root ("hdd:\") has only that one.
    if (folder.find('\\') + 1 >= folder.size()) return "";
    return folder.substr(0, folder.find_last_of('\\', folder.size() - 2) + 1);
}

std::string fitText(const std::string &text, int width) {
    if ((int)text.size() <= width) return text;
    return "..." + text.substr(text.size() - (width - 3)); // keep the end of long paths
}

struct Choice {
    std::string path;
    unsigned long long size;
};

// Lets the user pick a .nds file. Returns false if they leave to the dashboard.
bool browse(std::string *folder, Choice *choice) {
    std::vector<Entry> entries;
    std::vector<std::string> driveLabels;
    std::vector<int> drives; // indexes into kDrives that are reachable
    int selected = 0, top = 0;
    bool reload = true;
    WORD previous = 0xFFFF; // ignore buttons still held when the browser opens
    long long repeatAt = 0;
    const int listRows = menuPanel.rows - 6;

    for (;;) {
        if (reload) {
            reload = false;
            entries.clear();
            drives.clear();
            if (folder->empty()) {
                for (int i = 0; i < (int)(sizeof(kDrives) / sizeof(kDrives[0])); i++)
                    if (folderExists(kDrives[i].path)) drives.push_back(i);
            } else {
                entries = listFolder(*folder);
            }
            selected = 0;
            top = 0;
        }
        const int count = folder->empty() ? (int)drives.size() : (int)entries.size();

        // Draw.
        std::vector<std::string> lines;
        lines.push_back(std::string(kTitle) + " - choose a game");
        lines.push_back(fitText(folder->empty() ? "Drives" : *folder, menuPanel.cols));
        lines.push_back(std::string(menuPanel.cols, '-'));
        if (selected < top) top = selected;
        if (selected >= top + listRows) top = selected - listRows + 1;
        for (int i = top; i < count && i < top + listRows; i++) {
            std::string line;
            if (folder->empty()) {
                const Drive &drive = kDrives[drives[i]];
                line = std::string(" ") + drive.path + "  " + drive.label;
            } else if (entries[i].folder) {
                line = " [" + entries[i].name + "]";
            } else {
                char size[32];
                _snprintf(size, sizeof(size) - 1, "%6.1f MB", entries[i].size / 1048576.0);
                size[sizeof(size) - 1] = 0;
                line = " " + fitText(entries[i].name, menuPanel.cols - 12);
                line.resize(menuPanel.cols - 10, ' ');
                line += size;
            }
            lines.push_back(line);
        }
        if (count == 0)
            lines.push_back(folder->empty() ? " (no drives found)" : " (no folders or .nds files here)");
        while ((int)lines.size() < menuPanel.rows - 2) lines.push_back("");
        lines.push_back(std::string(menuPanel.cols, '-'));
        lines.push_back("A: open   B: up   Y: roms folder   LB/RB: page   Back: exit");
        setText(menuPanel, lines, count ? 3 + selected - top : -1);
        present(false, menuPanel);
        Sleep(16);

        // Input.
        Pad pad = readPad();
        WORD buttons = pad.buttons;
        if (pad.leftY > 16000) buttons |= XINPUT_GAMEPAD_DPAD_UP;
        if (pad.leftY < -16000) buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (previous == 0xFFFF) { previous = buttons ? buttons : 0; continue; }
        const WORD pressed = buttons & ~previous;
        previous = buttons;

        // Held up/down/LB/RB repeat after 0.4 s, every 0.08 s.
        const WORD moves = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
                           XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER;
        WORD move = pressed & moves;
        const long long now = xds::clockMicroseconds();
        if (move) repeatAt = now + 400000;
        else if ((buttons & moves) && now >= repeatAt) { move = buttons & moves; repeatAt = now + 80000; }
        if (move & XINPUT_GAMEPAD_DPAD_UP) selected--;
        if (move & XINPUT_GAMEPAD_DPAD_DOWN) selected++;
        if (move & XINPUT_GAMEPAD_LEFT_SHOULDER) selected -= listRows;
        if (move & XINPUT_GAMEPAD_RIGHT_SHOULDER) selected += listRows;
        selected = std::max(0, std::min(count - 1, selected));

        if (pressed & XINPUT_GAMEPAD_BACK) return false;
        if (pressed & XINPUT_GAMEPAD_Y) { *folder = kRomsFolder; reload = true; continue; }
        if (pressed & XINPUT_GAMEPAD_B) {
            if (!folder->empty()) {
                std::string from = *folder;
                *folder = parentFolder(*folder);
                reload = true;
                logLine("Browser: up from %s to %s", from.c_str(), folder->empty() ? "drives" : folder->c_str());
            }
            continue;
        }
        if ((pressed & XINPUT_GAMEPAD_A) && count > 0) {
            if (folder->empty()) {
                *folder = kDrives[drives[selected]].path;
                reload = true;
            } else if (entries[selected].folder) {
                *folder += entries[selected].name + "\\";
                reload = true;
            } else {
                choice->path = *folder + entries[selected].name;
                choice->size = entries[selected].size;
                return true;
            }
        }
    }
}

// ---------------------------------------------------------------- messages

// Shows a message until B is pressed.
void showMessage(const std::vector<std::string> &lines) {
    std::vector<std::string> wrapped;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = lines[i];
        do {
            wrapped.push_back(line.substr(0, menuPanel.cols));
            line = line.size() > (size_t)menuPanel.cols ? line.substr(menuPanel.cols) : "";
        } while (!line.empty());
    }
    wrapped.push_back("");
    wrapped.push_back("B: back");
    setText(menuPanel, wrapped);
    bool released = false; // B may still be held from the game
    for (;;) {
        present(false, menuPanel);
        const bool pressed = (readPad().buttons & XINPUT_GAMEPAD_B) != 0;
        if (pressed && released) return;
        if (!pressed) released = true;
        Sleep(16);
    }
}

void showError(const std::string &error, const std::string &detail) {
    std::vector<std::string> lines;
    lines.push_back(kTitle);
    lines.push_back("");
    lines.push_back(error);
    if (!detail.empty()) lines.push_back(detail);
    showMessage(lines);
}

std::string fileName(const std::string &path) {
    size_t slash = path.find_last_of('\\');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ---------------------------------------------------------------- game

// Runs one game until Back+Start is held or it stops. Returns to the browser.
void runGame(const Choice &rom) {
    const std::string name = fileName(rom.path);
    // Small ROMs are read into memory once; big ones (Pokemon B/W is 256MB)
    // are read from the file as the game asks for them.
    const bool romInRam = rom.size <= 128ULL * 1024 * 1024;
    static const int renderProcessors[3] = { 2, 3, 4 }; // 3D workers on cores 1 and 2
    xds::setThreadProcessors(renderProcessors, 3);

    emu::Options options;
    options.romPath = rom.path;
    options.dataFolder = "game:";
    options.biosFolder = "game:\\bios";
    options.romInRam = romInRam;
    options.renderThreads = 3;
    logLine("Starting %s (%llu bytes, romInRam=%d, 3D threads=3)", rom.path.c_str(), rom.size,
            romInRam ? 1 : 0);

    std::vector<std::string> lines;
    lines.push_back(kTitle);
    lines.push_back("");
    lines.push_back("Loading...");
    lines.push_back(name);
    setText(menuPanel, lines);
    present(false, menuPanel);

    std::string error;
    long long startTime = xds::clockMicroseconds();
    if (!emu::start(options, &error)) {
        logLine("Start failed: %s", error.c_str());
        showError(error, name);
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
    const std::string shortName = name.substr(0, statusPanel.cols);
    lines.clear();
    lines.push_back(kTitle);
    lines.push_back("");
    lines.push_back(shortName);
    setText(statusPanel, lines);

    for (;;) {
        long long before = xds::clockMicroseconds();
        if (!emu::run(&error)) {
            logLine("Core stopped: %s | %s", error.c_str(), emu::diagnostics().c_str());
            emu::stop();
            showError(error, name);
            return;
        }
        now = xds::clockMicroseconds();
        statsCore += now - before;

        const uint32_t completed = emu::completedFrames();
        if (completed == lastCompleted) {
            // runCore also returns when an emulated CPU halts or resumes.
            if (now - lastProgress > kStallMicroseconds || emu::unknownOpcodes() >= 4096) {
                logLine("No progress: %s", emu::diagnostics().c_str());
                emu::stop();
                showError("The game stopped running.", "See xds360.log");
                return;
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
            double coreMs = statsFrames ? statsCore / 1000.0 / statsFrames : 0.0;
            char text[64];
            text[sizeof(text) - 1] = 0;
            lines.clear();
            lines.push_back(kTitle);
            _snprintf(text, sizeof(text) - 1, "FPS   %5.1f", fps);
            lines.push_back(text);
            _snprintf(text, sizeof(text) - 1, "SPEED %5.0f%%", fps * 100.0 / 59.8261);
            lines.push_back(text);
            _snprintf(text, sizeof(text) - 1, "CORE  %5.1f ms", coreMs);
            lines.push_back(text);
            lines.push_back("");
            lines.push_back(shortName);
            lines.push_back("RT: touch  R-stick: move");
            lines.push_back("Back+Start: menu");
            setText(statusPanel, lines);
            if (now >= logStatsAt) {
                logLine("fps=%.1f video=%.1f core=%.1fms/frame", fps, statsVideo / seconds, coreMs);
                logStatsAt = now + 5000000;
            }
            statsStart = now;
            statsCore = 0;
            statsFrames = 0;
            statsVideo = 0;
        }

        present(haveFrame, statusPanel);

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

    logLine("Back to the menu from %s", name.c_str());
    emu::stop(); // writes the save
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
    createFolders();
    mountDrives();

    std::string folder = loadLastFolder();
    if (folder.empty() || !folderExists(folder)) folder = kRomsFolder;
    logLine("Browser starts in %s", folder.c_str());

    Choice choice;
    while (browse(&folder, &choice)) {
        saveLastFolder(folder);
        runGame(choice);
        waitForRelease(menuPanel); // Back+Start may still be held
    }
    quit();
}
