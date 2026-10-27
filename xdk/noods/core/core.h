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

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "defines.h"
#include "save_states.h"
#include "settings.h"
#include "arm/cp15.h"
#include "arm/interpreter.h"
#include "arm/timers.h"
#include "gpu/gpu.h"
#include "gpu/gpu_2d.h"
#include "gpu/gpu_3d.h"
#include "gpu/gpu_3d_renderer.h"
#include "hle/action_replay.h"
#include "hle/dldi.h"
#include "hle/hle_arm7.h"
#include "hle/hle_bios.h"
#include "io/aes.h"
#include "io/div_sqrt.h"
#include "io/i2c.h"
#include "io/input.h"
#include "io/ipc.h"
#include "io/rtc.h"
#include "io/spi.h"
#include "io/spu.h"
#include "io/wifi.h"
#include "memory/cartridge.h"
#include "memory/dma.h"
#include "memory/memory.h"
#include "memory/ndma.h"
#include "memory/sd_mmc.h"

enum CoreError {
    ERROR_NDS_BIOS = 1,
    ERROR_NDS_FIRM,
    ERROR_DSI_BIOS,
    ERROR_DSI_FIRM,
    ERROR_DSI_NAND,
    ERROR_ROM
};

enum SchedTask {
    UPDATE_RUN,
    RESET_CYCLES,
    CART9_WORD_READY,
    CART7_WORD_READY,
    DMA9_TRANSFER0,
    DMA9_TRANSFER1,
    DMA9_TRANSFER2,
    DMA9_TRANSFER3,
    DMA7_TRANSFER0,
    DMA7_TRANSFER1,
    DMA7_TRANSFER2,
    DMA7_TRANSFER3,
    NDS_SCANLINE256,
    NDS_SCANLINE355,
    GBA_SCANLINE240,
    GBA_SCANLINE308,
    GPU3D_COMMANDS,
    ARM9_INTERRUPT,
    ARM7_INTERRUPT,
    NDS_SPU_SAMPLE,
    GBA_SPU_SAMPLE,
    TIMER9_OVERFLOW0,
    TIMER9_OVERFLOW1,
    TIMER9_OVERFLOW2,
    TIMER9_OVERFLOW3,
    TIMER7_OVERFLOW0,
    TIMER7_OVERFLOW1,
    TIMER7_OVERFLOW2,
    TIMER7_OVERFLOW3,
    WIFI_COUNT_MS,
    WIFI_TRANS_REPLY,
    WIFI_TRANS_ACK,
    AES_UPDATE,
    NDMA9_UPDATE,
    NDMA7_UPDATE,
    SDMMC_READ_BLOCK,
    SDMMC_WRITE_BLOCK,
    MAX_TASKS
};

struct SchedEvent {
    SchedTask task;
    uint32_t cycles;

    SchedEvent(SchedTask task, uint32_t cycles): task(task), cycles(cycles) {}
    bool operator<(const SchedEvent &event) const { return cycles < event.cycles; }
};

class Core {
public:
    int id;
    int fps;
    bool arm7Hle;
    bool dsiMode;
    bool gbaMode;

    ActionReplay actionReplay;
    Aes aes;
    CartridgeGba cartridgeGba;
    CartridgeNds cartridgeNds;
    Cp15 cp15;
    DivSqrt divSqrt;
    Dldi dldi;
    XdsArray<Dma, 2> dma;
    Gpu gpu;
    XdsArray<Gpu2D, 2> gpu2D;
    Gpu3D gpu3D;
    Gpu3DRenderer gpu3DRenderer;
    HleArm7 hleArm7;
    XdsArray<HleBios, 3> hleBios;
    I2c i2c;
    Input input;
    XdsArray<Interpreter, 2> interpreter;
    Ipc ipc;
    Memory memory;
    XdsArray<Ndma, 2> ndma;
    Rtc rtc;
    SaveStates saveStates;
    SdMmc sdMmc;
    Spi spi;
    Spu spu;
    XdsArray<Timers, 2> timers;
    Wifi wifi;

    std::atomic<bool> running;
    std::vector<SchedEvent> events;
#ifndef NOODS_XENON
    std::function<void()> tasks[MAX_TASKS];
#endif
    uint32_t globalCycles;
#ifdef NOODS_XENON
    // Frontends must distinguish a real emulated frame boundary from the
    // internal scheduler returns caused by CPU halt/run state changes.
    uint32_t completedFrames;
#endif

    Core(std::string ndsRom = "", std::string gbaRom = "", int id = 0, int ndsRomFd = -1, int gbaRomFd = -1,
        int ndsSaveFd = -1, int gbaSaveFd = -1, int ndsStateFd = -1, int gbaStateFd = -1, int ndsCheatFd = -1);
    void saveState(FILE *file);
    void loadState(FILE *file);

    void runCore() { (*runFunc)(*this); }
    void schedule(SchedTask task, uint32_t cycles);
#ifdef NOODS_XENON
    void runTask(SchedTask task);
#endif
    void enterGbaMode();
    void endFrame();

private:
    bool realGbaBios;
    void (*runFunc)(Core&);
    std::chrono::steady_clock::time_point lastFpsTime;
    int fpsCount;

    void updateRun();
    void resetCycles();
};
