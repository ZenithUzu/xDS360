// SPDX-License-Identifier: GPL-3.0-or-later
// PC simulator for xdk/frontend/main.cpp. Implements the fake <xtl.h> just
// enough to run the real frontend: Direct3D draws into a 1280x720 picture,
// XInput replays a button script, and "game:\", "hdd:\"... map to folders.
// Used by tools/xdk/sim/run_sim.sh to take screenshots of the menu and a game.
#include "xtl.h"
#include "xaudio2.h"

#include <dirent.h>
#include <pthread.h>
#include <time.h>
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
std::string pendingShot;
size_t stepIndex = 0;

WORD buttonMask(const std::string &names) {
    static const struct { const char *name; WORD mask; } table[] = {
        { "A", XINPUT_GAMEPAD_A }, { "B", XINPUT_GAMEPAD_B }, { "X", XINPUT_GAMEPAD_X },
        { "Y", XINPUT_GAMEPAD_Y }, { "UP", XINPUT_GAMEPAD_DPAD_UP }, { "DOWN", XINPUT_GAMEPAD_DPAD_DOWN },
        { "LEFT", XINPUT_GAMEPAD_DPAD_LEFT }, { "RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT },
        { "START", XINPUT_GAMEPAD_START }, { "BACK", XINPUT_GAMEPAD_BACK },
        { "LB", XINPUT_GAMEPAD_LEFT_SHOULDER }, { "RB", XINPUT_GAMEPAD_RIGHT_SHOULDER },
        { "LS", XINPUT_GAMEPAD_LEFT_THUMB }, { "RS", XINPUT_GAMEPAD_RIGHT_THUMB }
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
float tint[4] = { 1, 1, 1, 1 };
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
        // Saved at the end of the frame: ConsoleUI reads input before it has drawn everything.
        if (!step.shot.empty()) { pendingShot = step.shot; stepIndex++; return ERROR_SUCCESS; } // no buttons this frame
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
struct ThreadStart { LPTHREAD_START_ROUTINE routine; LPVOID parameter; };
static void *runThread(void *p) {
    ThreadStart start = *(ThreadStart*)p;
    delete (ThreadStart*)p;
    start.routine(start.parameter);
    return 0;
}
HANDLE WINAPI CreateThread(void *, DWORD, LPTHREAD_START_ROUTINE routine, LPVOID parameter, DWORD, DWORD *) {
    ThreadStart *start = new ThreadStart;
    start->routine = routine;
    start->parameter = parameter;
    pthread_t thread;
    pthread_create(&thread, 0, runThread, start); // starts at once: ResumeThread is a no-op
    pthread_detach(thread);
    return (HANDLE)1;
}
DWORD WINAPI ResumeThread(HANDLE) { return 0; }
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
DWORD IDirect3DTexture9::Release() { delete static_cast<SimTexture*>(this); return 0; }

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
void IDirect3DDevice9::SetPixelShaderConstantF(UINT, const float *values, DWORD) { memcpy(tint, values, sizeof(tint)); }
void IDirect3DDevice9::SetTexture(DWORD, IDirect3DTexture9 *texture) { bound = static_cast<SimTexture*>(texture); }
HRESULT IDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, const void *data, UINT stride) {
    // Strip order: top-left, top-right, bottom-left, bottom-right (x, y, z, w, u, v).
    // Point sampling; the pixel shader's .bgra swizzle, tint and alpha blending.
    const float *v[4];
    for (int i = 0; i < 4; i++) v[i] = (const float*)((const char*)data + i * stride);
    float x0 = v[0][0] + 0.5f, y0 = v[0][1] + 0.5f, x1 = v[3][0] + 0.5f, y1 = v[3][1] + 0.5f;
    if (!bound || x1 <= x0 || y1 <= y0) return 0;
    for (int py = (int)y0; py < (int)y1 && py < 720; py++) {
        if (py < 0) continue;
        float fy = (py + 0.5f - y0) / (y1 - y0);
        for (int px = (int)x0; px < (int)x1 && px < 1280; px++) {
            if (px < 0) continue;
            float fx = (px + 0.5f - x0) / (x1 - x0);
            float u = (v[0][4] * (1 - fx) + v[1][4] * fx) * (1 - fy) + (v[2][4] * (1 - fx) + v[3][4] * fx) * fy;
            float t = (v[0][5] * (1 - fx) + v[1][5] * fx) * (1 - fy) + (v[2][5] * (1 - fx) + v[3][5] * fx) * fy;
            int tx = (int)(u * bound->width), ty = (int)(t * bound->height);
            tx = tx < 0 ? 0 : (tx >= bound->width ? bound->width - 1 : tx);
            ty = ty < 0 ? 0 : (ty >= bound->height ? bound->height - 1 : ty);
            unsigned c = bound->pixels[ty * bound->width + tx]; // 0xAABBGGRR as stored by NooDS
            float r = (c & 0xFF) * tint[0], g = ((c >> 8) & 0xFF) * tint[1];
            float b = ((c >> 16) & 0xFF) * tint[2], a = (c >> 24) / 255.0f * tint[3];
            unsigned &d = framebuffer[py * 1280 + px];
            float dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
            d = 0xFF000000 | ((unsigned)(r * a + dr * (1 - a)) << 16) | ((unsigned)(g * a + dg * (1 - a)) << 8) |
                (unsigned)(b * a + db * (1 - a));
        }
    }
    return 0;
}
DWORD IDirect3DDevice9::InsertFence() { return ++fenceCounter; }
void IDirect3DDevice9::BlockOnFence(DWORD) {}
HRESULT IDirect3DDevice9::Present(const void *, const void *, void *, const void *) {
    if (!pendingShot.empty()) { saveShot(pendingShot); pendingShot.clear(); }
    usleep(16667); // vsync at 60 Hz, so script waits count real frames
    return 0;
}
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

// XAudio2: a voice that plays its buffers at 32768 Hz in real time.
static double audioStart = 0;
static unsigned long long submittedSamples = 0;
static double nowSeconds() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static IXAudio2 xaudioObject;
static IXAudio2MasteringVoice masterObject;
static IXAudio2SourceVoice voiceObject;
HRESULT XAudio2Create(IXAudio2 **out, UINT, DWORD) { *out = &xaudioObject; return 0; }
HRESULT IXAudio2::CreateMasteringVoice(IXAudio2MasteringVoice **out) { *out = &masterObject; return 0; }
HRESULT IXAudio2::CreateSourceVoice(IXAudio2SourceVoice **out, const WAVEFORMATEX *) { *out = &voiceObject; return 0; }
HRESULT IXAudio2SourceVoice::Start(UINT) { audioStart = nowSeconds(); return 0; }
HRESULT IXAudio2SourceVoice::SubmitSourceBuffer(const XAUDIO2_BUFFER *b) { submittedSamples += b->AudioBytes / 4; return 0; }
void IXAudio2SourceVoice::GetState(XAUDIO2_VOICE_STATE *state) {
    double played = (nowSeconds() - audioStart) * 32768;
    double queued = submittedSamples > played ? submittedSamples - played : 0;
    if (played > submittedSamples) audioStart = nowSeconds() - submittedSamples / 32768.0; // underrun
    state->BuffersQueued = (UINT)((queued + 1023) / 1024);
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
