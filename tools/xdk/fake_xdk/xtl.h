// Fake <xtl.h> for syntax checks on a PC (tools/xdk/check_syntax.sh).
// Declarations only, written from the public XDK/Win32 API shapes. It is NOT
// the Microsoft SDK and is never used to build the real .xex.
#pragma once

#include <stddef.h>
#include <string.h>

typedef unsigned int DWORD; // 32 bits, as on the Xbox 360 (unsigned long is 64-bit on Linux)
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef short SHORT;
typedef long LONG;
typedef int BOOL;
typedef unsigned int UINT;
typedef char CHAR;
typedef void VOID;
typedef void *HANDLE;
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef long HRESULT;
typedef DWORD (__stdcall *LPTHREAD_START_ROUTINE)(LPVOID);
typedef union { struct { DWORD LowPart; LONG HighPart; }; long long QuadPart; } LARGE_INTEGER;
typedef struct { DWORD dwFileAttributes; DWORD nFileSizeHigh; DWORD nFileSizeLow; CHAR cFileName[260]; } WIN32_FIND_DATA;
typedef struct { void *opaque[8]; } CRITICAL_SECTION;
typedef struct { DWORD dwLength, dwMemoryLoad; size_t dwTotalPhys, dwAvailPhys, dwTotalPageFile, dwAvailPageFile,
                 dwTotalVirtual, dwAvailVirtual; } MEMORYSTATUS;

#define WINAPI __stdcall
#define TRUE 1
#define FALSE 0
#define INFINITE 0xFFFFFFFF
#define CREATE_SUSPENDED 0x4
#define ERROR_SUCCESS 0L
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define ZeroMemory(p, n) memset((p), 0, (n))
#ifndef NOMINMAX
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

extern "C" {
HANDLE WINAPI CreateThread(void *, DWORD, LPTHREAD_START_ROUTINE, LPVOID, DWORD, DWORD *);
DWORD WINAPI ResumeThread(HANDLE);
DWORD WINAPI XSetThreadProcessor(HANDLE, DWORD);
HANDLE WINAPI GetCurrentThread();
DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
HANDLE WINAPI CreateEvent(void *, BOOL, BOOL, LPCSTR);
BOOL WINAPI SetEvent(HANDLE);
BOOL WINAPI CloseHandle(HANDLE);
void WINAPI InitializeCriticalSection(CRITICAL_SECTION *);
void WINAPI DeleteCriticalSection(CRITICAL_SECTION *);
void WINAPI EnterCriticalSection(CRITICAL_SECTION *);
BOOL WINAPI TryEnterCriticalSection(CRITICAL_SECTION *);
void WINAPI LeaveCriticalSection(CRITICAL_SECTION *);
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *);
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *);
void WINAPI Sleep(DWORD);
LONG WINAPI InterlockedExchange(volatile LONG *, LONG);
BOOL WINAPI CreateDirectoryA(LPCSTR, void *);
BOOL WINAPI CreateDirectory(LPCSTR, void *);
void WINAPI GlobalMemoryStatus(MEMORYSTATUS *);
DWORD WINAPI GetFileAttributes(LPCSTR);
HANDLE WINAPI FindFirstFile(LPCSTR, WIN32_FIND_DATA *);
BOOL WINAPI FindNextFile(HANDLE, WIN32_FIND_DATA *);
BOOL WINAPI FindClose(HANDLE);
DWORD WINAPI XLaunchNewImage(LPCSTR, DWORD);
}
void MemoryBarrier();

// XInput
#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_START 0x0010
#define XINPUT_GAMEPAD_BACK 0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB 0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB 0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_GAMEPAD_X 0x4000
#define XINPUT_GAMEPAD_Y 0x8000
typedef struct { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger; SHORT sThumbLX; SHORT sThumbLY; SHORT sThumbRX; SHORT sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
extern "C" DWORD WINAPI XInputGetState(DWORD, XINPUT_STATE *);

// Direct3D 9 (Xbox 360 flavour), only what xDS360 uses.
#define D3D_SDK_VERSION 0
#define D3DCOLOR_XRGB(r, g, b) ((DWORD)(0xFF000000 | ((r) << 16) | ((g) << 8) | (b)))
enum D3DFORMAT { D3DFMT_A8R8G8B8, D3DFMT_LIN_A8R8G8B8 };
enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_NONE };
enum D3DSWAPEFFECT { D3DSWAPEFFECT_DISCARD };
enum D3DDEVTYPE { D3DDEVTYPE_HAL };
enum D3DPOOL { D3DPOOL_DEFAULT };
enum D3DPRIMITIVETYPE { D3DPT_TRIANGLESTRIP };
enum D3DRENDERSTATETYPE { D3DRS_VIEWPORTENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ZENABLE,
                          D3DRS_SCISSORTESTENABLE, D3DRS_CULLMODE, D3DRS_COLORWRITEENABLE, D3DRS_SRCBLEND,
                          D3DRS_DESTBLEND };
enum { D3DBLEND_SRCALPHA = 5, D3DBLEND_INVSRCALPHA = 6 };
enum { D3DCULL_NONE };
enum D3DSAMPLERSTATETYPE { D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV };
enum { D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTADDRESS_CLAMP };
enum { D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT4, D3DDECLTYPE_UNUSED };
enum { D3DDECLMETHOD_DEFAULT };
enum { D3DDECLUSAGE_POSITION, D3DDECLUSAGE_TEXCOORD };
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x40
#define D3DPRESENT_INTERVAL_IMMEDIATE 0x80000000
#define D3DPRESENT_INTERVAL_ONE 0x1
#define D3DCLEAR_TARGET 1
#define D3DDECL_END() { 0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0 }
typedef struct { WORD Stream; WORD Offset; DWORD Type; BYTE Method; BYTE Usage; BYTE UsageIndex; } D3DVERTEXELEMENT9;
typedef struct { int Pitch; void *pBits; } D3DLOCKED_RECT;
typedef struct { UINT BackBufferWidth, BackBufferHeight; D3DFORMAT BackBufferFormat; UINT BackBufferCount;
                 D3DMULTISAMPLE_TYPE MultiSampleType; D3DSWAPEFFECT SwapEffect; UINT PresentationInterval; } D3DPRESENT_PARAMETERS;

struct ID3DXBuffer { void *GetBufferPointer(); DWORD Release(); };
struct IDirect3DVertexShader9 {};
struct IDirect3DPixelShader9 {};
struct IDirect3DVertexDeclaration9 {};
struct IDirect3DTexture9 {
    HRESULT LockRect(UINT, D3DLOCKED_RECT *, const void *, DWORD);
    HRESULT UnlockRect(UINT);
    DWORD Release();
};
struct IDirect3DDevice9 {
    HRESULT CreateVertexShader(const DWORD *, IDirect3DVertexShader9 **);
    HRESULT CreatePixelShader(const DWORD *, IDirect3DPixelShader9 **);
    HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9 *, IDirect3DVertexDeclaration9 **);
    HRESULT CreateTexture(UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9 **, HANDLE *);
    HRESULT Clear(DWORD, const void *, DWORD, DWORD, float, DWORD);
    void BeginScene();
    void EndScene();
    void SetRenderState(D3DRENDERSTATETYPE, DWORD);
    void SetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD);
    void SetVertexDeclaration(IDirect3DVertexDeclaration9 *);
    void SetVertexShader(IDirect3DVertexShader9 *);
    void SetPixelShader(IDirect3DPixelShader9 *);
    void SetPixelShaderConstantF(UINT, const float *, DWORD);
    void SetTexture(DWORD, IDirect3DTexture9 *);
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, const void *, UINT);
    DWORD InsertFence();
    void BlockOnFence(DWORD);
    HRESULT Present(const void *, const void *, void *, const void *);
};
struct IDirect3D9 {
    HRESULT CreateDevice(UINT, D3DDEVTYPE, void *, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
};
IDirect3D9 *WINAPI Direct3DCreate9(UINT);
HRESULT WINAPI D3DXCompileShader(LPCSTR, UINT, const void *, void *, LPCSTR, LPCSTR, DWORD,
                                 ID3DXBuffer **, ID3DXBuffer **, void **);
