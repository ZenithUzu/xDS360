// SPDX-License-Identifier: GPL-3.0-or-later
#include "emu.h"

#include "core.h"
#include "settings.h"

#include <cstdio>
#include <new>

#if !defined(_MSC_VER)
#define _snprintf snprintf // PC tests
#endif

namespace emu {
namespace {

Core *core = 0;

const char *errorText(int error) {
    switch (error) {
    case ERROR_NDS_BIOS: return "DS BIOS could not be loaded";
    case ERROR_NDS_FIRM: return "DS firmware could not be loaded";
    case ERROR_DSI_BIOS: return "DSi BIOS is missing";
    case ERROR_DSI_FIRM: return "DSi firmware is missing";
    case ERROR_DSI_NAND: return "DSi NAND is missing";
    case ERROR_ROM: return "The ROM could not be opened or read";
    default: return "NooDS could not start";
    }
}

} // namespace

bool start(const Options &options, std::string *error) {
    // Same core settings as the XenonDS v0.9.1 hardware build.
    Settings::basePath = options.biosFolder;
    Settings::directBoot = 1;
    Settings::romInRam = options.romInRam ? 1 : 0;
    Settings::fpsLimiter = 0;
    Settings::frameskip = 0;
    Settings::threaded2D = 0;
    Settings::threaded3D = options.renderThreads;
    Settings::highRes3D = 0;
    Settings::screenGhost = 0;
    Settings::emulateAudio = 0;
    Settings::savesFolder = 0;
    Settings::statesFolder = 0;
    Settings::cheatsFolder = 0;
    Settings::screenFilter = 0;
    Settings::dsiMode = 0;
    Settings::arm7Hle = 0;
    Settings::gbaBiosPath = options.biosFolder + "\\gba_bios.bin";
    Settings::ndsBios9Path = options.biosFolder + "\\bios9.bin";
    Settings::ndsBios7Path = options.biosFolder + "\\bios7.bin";
    Settings::ndsFirmPath = options.biosFolder + "\\firmware.bin";

    try {
        core = new Core(options.romPath);
    } catch (int code) {
        *error = errorText(code);
        core = 0;
        return false;
    } catch (const std::bad_alloc&) {
        *error = "Not enough memory for the core";
        core = 0;
        return false;
    } catch (...) {
        *error = "Unexpected error while starting the core";
        core = 0;
        return false;
    }
    return true;
}

bool run(std::string *error) {
    try {
        core->runCore();
    } catch (int code) {
        *error = errorText(code);
        return false;
    } catch (const std::bad_alloc&) {
        *error = "Out of memory while running the game";
        return false;
    } catch (...) {
        *error = "Unexpected error while running the game";
        return false;
    }
    return true;
}

uint32_t completedFrames() {
    return core ? core->completedFrames : 0;
}

bool takeFrame(uint16_t *pixels) {
    return core && core->gpu.getFrameXenon(pixels, false);
}

void setInput(uint16_t buttons, bool touching, int touchX, int touchY) {
    for (int key = 0; key < 12; key++) {
        if (buttons & (1 << key)) core->input.pressKey(key);
        else core->input.releaseKey(key);
    }
    if (touching) {
        core->spi.setTouch(touchX, touchY);
        core->input.pressScreen();
    } else {
        core->spi.clearTouch();
        core->input.releaseScreen();
    }
}

void writeSave() {
    if (core) core->cartridgeNds.writeSave();
}

std::string diagnostics() {
    if (!core) return "core not running";
    char text[160];
    _snprintf(text, sizeof(text) - 1, "ARM9 PC=%08X op=%08X bad=%u | ARM7 PC=%08X op=%08X bad=%u",
        core->interpreter[0].getPC(), core->interpreter[0].lastUnknownOpcode,
        core->interpreter[0].unknownOpcodeCount, core->interpreter[1].getPC(),
        core->interpreter[1].lastUnknownOpcode, core->interpreter[1].unknownOpcodeCount);
    text[sizeof(text) - 1] = 0;
    return text;
}

uint32_t unknownOpcodes() {
    if (!core) return 0;
    return core->interpreter[0].unknownOpcodeCount + core->interpreter[1].unknownOpcodeCount;
}

void stop() {
    if (!core) return;
    delete core; // the Cartridge destructor writes the save
    core = 0;
}

} // namespace emu
