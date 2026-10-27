// SPDX-License-Identifier: GPL-3.0-or-later
// Thin wrapper around the NooDS core. It never includes <xtl.h>, and main.cpp
// never includes NooDS headers, so Windows macros and NooDS names stay apart.
#pragma once

#include <stdint.h>
#include <string>

namespace emu {

static const int kWidth = 256;
static const int kHeight = 192 * 2; // both screens, top first

// DS button bits, in NooDS key order.
enum {
    kA = 1 << 0, kB = 1 << 1, kSelect = 1 << 2, kStart = 1 << 3,
    kRight = 1 << 4, kLeft = 1 << 5, kUp = 1 << 6, kDown = 1 << 7,
    kR = 1 << 8, kL = 1 << 9, kX = 1 << 10, kY = 1 << 11
};

struct Options {
    std::string romPath;    // game:\roms\game.nds
    std::string dataFolder; // the save goes in <dataFolder>\saves (game: on the Xbox)
    std::string biosFolder; // where optional bios7/bios9/firmware files live
    bool romInRam;          // read the whole ROM into memory at start
    int renderThreads;      // software-3D worker threads (0 = none)
};

// Starts the core. On failure returns false and fills `error`.
bool start(const Options &options, std::string *error);

// Runs the core until the next frame end or scheduler return.
// Returns false (and fills `error`) if the core stopped with an exception.
bool run(std::string *error);

// DS frames completed since start.
uint32_t completedFrames();

// Copies the newest finished frame (BGR555, 256x384). False if none is new.
bool takeFrame(uint16_t *pixels);

void setInput(uint16_t buttons, bool touching, int touchX, int touchY);

// Writes the cartridge save if the game changed it.
void writeSave();

// CPU state for the log when something goes wrong.
std::string diagnostics();
uint32_t unknownOpcodes();

void stop();

} // namespace emu
