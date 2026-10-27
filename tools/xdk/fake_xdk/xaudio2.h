// Fake <xaudio2.h> for PC syntax checks and the simulator (see xtl.h here).
#pragma once
#include "xtl.h"

#define XAUDIO2_DEFAULT_PROCESSOR 0x20
#define WAVE_FORMAT_PCM 1
typedef struct { WORD wFormatTag; WORD nChannels; DWORD nSamplesPerSec; DWORD nAvgBytesPerSec;
                 WORD nBlockAlign; WORD wBitsPerSample; WORD cbSize; } WAVEFORMATEX;
typedef struct { DWORD Flags; UINT AudioBytes; const BYTE *pAudioData; UINT PlayBegin, PlayLength, LoopBegin,
                 LoopLength, LoopCount; void *pContext; } XAUDIO2_BUFFER;
typedef struct { void *pCurrentBufferContext; UINT BuffersQueued; unsigned long long SamplesPlayed; }
    XAUDIO2_VOICE_STATE;
struct IXAudio2SourceVoice {
    HRESULT Start(UINT);
    HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER *);
    void GetState(XAUDIO2_VOICE_STATE *);
};
struct IXAudio2MasteringVoice {};
struct IXAudio2 {
    HRESULT CreateMasteringVoice(IXAudio2MasteringVoice **);
    HRESULT CreateSourceVoice(IXAudio2SourceVoice **, const WAVEFORMATEX *);
};
HRESULT XAudio2Create(IXAudio2 **, UINT, DWORD);
