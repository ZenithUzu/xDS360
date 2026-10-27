// SPDX-License-Identifier: GPL-3.0-or-later
// PC simulator for xdk/frontend/main.cpp. Implements the fake <xtl.h> just
// enough to run the real frontend: Direct3D draws into a 1280x720 picture,
// XInput replays a button script, and "game:\", "hdd:\"... map to folders.
// Used by tools/xdk/sim/run_sim.sh to take screenshots of the menu and a game.
#include "xtl.h"

#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace {

std::string root;                  // SIM_ROOT: stands for "game:\"
std::vector<unsigned> framebuffer(1280 * 720);
std::string shotFolder;            // SIM_SHOTS

std::string hostPath(const char *path) {
    std::string p = path;
    size_t colon = p.find(':');
    if (colon != std::string::npos && colon < 10) {
        std::string drive = p.substr(0, colon);
        p = (drive == "game" ? root : root + "/" + drive) + "/" + p.substr(colon + 1);
    }
    for (size_t i = 0; i < p.size(); i++)
        if (p[i] == '\\') p[i] = '/';
    return p;
}

void saveShot(const std::string &name) {
    std::string path = shotFolder + "/" + name + ".ppm";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n1280 720\n255\n");
    for (size_t i = 0; i < framebuffer.size(); i++) {
        unsigned c = framebuffer[i];
        unsigned char rgb[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("SIM shot %s\n", path.c_str());
}

// ---- button script: "wait:N press:A hold:BACK+START:N shot:name"
struct Step { WORD buttons; int ticks; std::string shot; };
std::vector<Step> script;
size_t stepIndex = 0;

WORD buttonMask(const std::string &names) {
    static const struct { const char *name; WORD mask; } table[] = {
        { "A", XINPUT_GAMEPAD_A }, { "B", XINPUT_GAMEPAD_B }, { "X", XINPUT_GAMEPAD_X },
        { "Y", XINPUT_GAMEPAD_Y }, { "UP", XINPUT_GAMEPAD_DPAD_UP }, { "DOWN", XINPUT_GAMEPAD_DPAD_DOWN },
        { "LEFT", XINPUT_GAMEPAD_DPAD_LEFT }, { "RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT },
        { "START", XINPUT_GAMEPAD_START }, { "BACK", XINPUT_GAMEPAD_BACK },
        { "LB", XINPUT_GAMEPAD_LEFT_SHOULDER }, { "RB", XINPUT_GAMEPAD_RIGHT_SHOULDER }
    };
    WORD mask = 0;
    size_t start = 0;
    while (start <= names.size()) {
        size_t plus = names.find('+', start);
        std::string one = names.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
            if (one == table[i].name) mask |= table[i].mask;
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    return mask;
}

void parseScript(const char *text) {
    std::string all = text ? text : "";
    size_t start = 0;
    while (start < all.size()) {
        size_t space = all.find(' ', start);
        std::string token = all.substr(start, space == std::string::npos ? std::string::npos : space - start);
        start = space == std::string::npos ? all.size() : space + 1;
        if (token.empty()) continue;
        Step step = { 0, 1, "" };
        if (token.compare(0, 5, "wait:") == 0) {
            step.ticks = atoi(token.c_str() + 5);
            script.push_back(step);
        } else if (token.compare(0, 6, "press:") == 0) {
            step.buttons = buttonMask(token.substr(6));
            script.push_back(step);
            Step release = { 0, 1, "" };
            script.push_back(release);
        } else if (token.compare(0, 5, "hold:") == 0) {
            size_t colon = token.rfind(':');
            step.buttons = buttonMask(token.substr(5, colon - 5));
            step.ticks = atoi(token.c_str() + colon + 1);
            script.push_back(step);
        } else if (token.compare(0, 5, "shot:") == 0) {
            step.ticks = 0;
            step.shot = token.substr(5);
            script.push_back(step);
        }
    }
}

// ---- fake Direct3D objects
struct SimTexture : IDirect3DTexture9 {
    int width, height;
    std::vector<unsigned> pixels;
};
SimTexture *bound = 0;
ID3DXBuffer shaderBuffer;
IDirect3DVertexShader9 vertexShaderObject;
IDirect3DPixelShader9 pixelShaderObject;
IDirect3DVertexDeclaration9 declarationObject;
IDirect3DDevice9 deviceObject;
IDirect3D9 d3dObject;
DWORD fenceCounter = 0;

} // namespace

// ---- Win32 / XDK functions used by the frontend
extern "C" {
DWORD WINAPI XInputGetState(DWORD, XINPUT_STATE *state) {
    memset(state, 0, sizeof(*state));
    while (stepIndex < script.size()) {
        Step &step = script[stepIndex];
        if (!step.shot.empty()) { saveShot(step.shot); stepIndex++; continue; }
        if (step.ticks <= 0) { stepIndex++; continue; }
        step.ticks--;
        state->Gamepad.wButtons = step.buttons;
        return ERROR_SUCCESS;
    }
    printf("SIM script finished\n");
    fflush(stdout);
    _exit(0);
}
void WINAPI Sleep(DWORD milliseconds) { usleep(milliseconds * 1000); }
BOOL WINAPI CreateDirectory(LPCSTR path, void *) { return mkdir(hostPath(path).c_str(), 0777) == 0; }
DWORD WINAPI GetFileAttributes(LPCSTR path) {
    struct stat info;
    if (stat(hostPath(path).c_str(), &info) != 0) return (DWORD)-1; // INVALID_FILE_ATTRIBUTES
    return S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
}

struct FindState { DIR *dir; std::string folder, pattern; };

static bool nextMatch(FindState *find, WIN32_FIND_DATA *data) {
    while (dirent *entry = readdir(find->dir)) {
        if (fnmatch(find->pattern.c_str(), entry->d_name, FNM_CASEFOLD) != 0) continue;
        std::string full = find->folder + "/" + entry->d_name;
        struct stat info;
        if (stat(full.c_str(), &info) != 0) continue;
        memset(data, 0, sizeof(*data));
        data->dwFileAttributes = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
        data->nFileSizeLow = (DWORD)(info.st_size & 0xFFFFFFFF);
        data->nFileSizeHigh = (DWORD)((unsigned long long)info.st_size >> 32);
        strncpy(data->cFileName, entry->d_name, sizeof(data->cFileName) - 1);
        return true;
    }
    return false;
}

HANDLE WINAPI FindFirstFile(LPCSTR pattern, WIN32_FIND_DATA *data) {
    std::string path = hostPath(pattern);
    size_t slash = path.rfind('/');
    FindState *find = new FindState;
    find->folder = path.substr(0, slash);
    find->pattern = path.substr(slash + 1);
    find->dir = opendir(find->folder.c_str());
    if (!find->dir || !nextMatch(find, data)) {
        if (find->dir) closedir(find->dir);
        delete find;
        return INVALID_HANDLE_VALUE;
    }
    return find;
}
BOOL WINAPI FindNextFile(HANDLE handle, WIN32_FIND_DATA *data) { return nextMatch((FindState*)handle, data); }
BOOL WINAPI FindClose(HANDLE handle) {
    closedir(((FindState*)handle)->dir);
    delete (FindState*)handle;
    return TRUE;
}
DWORD WINAPI XLaunchNewImage(LPCSTR, DWORD) {
    printf("SIM XLaunchNewImage (back to the dashboard)\n");
    fflush(stdout);
    _exit(0);
}
DWORD WINAPI XSetThreadProcessor(HANDLE, DWORD) { return 0; }
void WINAPI GlobalMemoryStatus(MEMORYSTATUS *m) { m->dwTotalPhys = 512u << 20; m->dwAvailPhys = 400u << 20; }
HANDLE WINAPI GetCurrentThread() { return 0; }
long ObCreateSymbolicLink(void *, void *) { return 0; } // drives map to SIM_ROOT/<name>

}

void *ID3DXBuffer::GetBufferPointer() { return this; }
DWORD ID3DXBuffer::Release() { return 0; }

HRESULT IDirect3DTexture9::LockRect(UINT, D3DLOCKED_RECT *locked, const void *, DWORD) {
    SimTexture *t = static_cast<SimTexture*>(this);
    locked->pBits = &t->pixels[0];
    locked->Pitch = t->width * 4;
    return 0;
}
HRESULT IDirect3DTexture9::UnlockRect(UINT) { return 0; }

HRESULT IDirect3DDevice9::CreateVertexShader(const DWORD *, IDirect3DVertexShader9 **out) { *out = &vertexShaderObject; return 0; }
HRESULT IDirect3DDevice9::CreatePixelShader(const DWORD *, IDirect3DPixelShader9 **out) { *out = &pixelShaderObject; return 0; }
HRESULT IDirect3DDevice9::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *, IDirect3DVertexDeclaration9 **out) {
    *out = &declarationObject;
    return 0;
}
HRESULT IDirect3DDevice9::CreateTexture(UINT w, UINT h, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9 **out, HANDLE *) {
    SimTexture *t = new SimTexture;
    t->width = w;
    t->height = h;
    t->pixels.assign(w * h, 0xFF000000);
    *out = t;
    return 0;
}
HRESULT IDirect3DDevice9::Clear(DWORD, const void *, DWORD, DWORD color, float, DWORD) {
    for (size_t i = 0; i < framebuffer.size(); i++) framebuffer[i] = color;
    return 0;
}
void IDirect3DDevice9::BeginScene() {}
void IDirect3DDevice9::EndScene() {}
void IDirect3DDevice9::SetRenderState(D3DRENDERSTATETYPE, DWORD) {}
void IDirect3DDevice9::SetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD) {}
void IDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9 *) {}
void IDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9 *) {}
void IDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9 *) {}
void IDirect3DDevice9::SetTexture(DWORD, IDirect3DTexture9 *texture) { bound = static_cast<SimTexture*>(texture); }
HRESULT IDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, const void *data, UINT stride) {
    // Strip order: top-left, top-right, bottom-left, bottom-right; point sampling.
    const float *v0 = (const float*)data;
    const float *v3 = (const float*)((const char*)data + 3 * stride);
    float x0 = v0[0] + 0.5f, y0 = v0[1] + 0.5f, x1 = v3[0] + 0.5f, y1 = v3[1] + 0.5f;
    if (!bound) return 0;
    for (int py = (int)y0; py < (int)y1 && py < 720; py++) {
        if (py < 0) continue;
        int ty = (int)((py + 0.5f - y0) / (y1 - y0) * bound->height);
        for (int px = (int)x0; px < (int)x1 && px < 1280; px++) {
            if (px < 0) continue;
            int tx = (int)((px + 0.5f - x0) / (x1 - x0) * bound->width);
            framebuffer[py * 1280 + px] = bound->pixels[ty * bound->width + tx];
        }
    }
    return 0;
}
DWORD IDirect3DDevice9::InsertFence() { return ++fenceCounter; }
void IDirect3DDevice9::BlockOnFence(DWORD) {}
HRESULT IDirect3DDevice9::Present(const void *, const void *, void *, const void *) { return 0; }
HRESULT IDirect3D9::CreateDevice(UINT, D3DDEVTYPE, void *, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **out) {
    *out = &deviceObject;
    return 0;
}
IDirect3D9 *WINAPI Direct3DCreate9(UINT) { return &d3dObject; }
HRESULT WINAPI D3DXCompileShader(LPCSTR, UINT, const void *, void *, LPCSTR, LPCSTR, DWORD,
                                 ID3DXBuffer **code, ID3DXBuffer **errors, void **) {
    *code = &shaderBuffer;
    if (errors) *errors = 0;
    return 0;
}

void xdkMain();

int main() {
    const char *r = getenv("SIM_ROOT");
    const char *s = getenv("SIM_SHOTS");
    root = r ? r : ".";
    shotFolder = s ? s : ".";
    parseScript(getenv("SIM_SCRIPT"));
    xdkMain();
    return 0;
}
