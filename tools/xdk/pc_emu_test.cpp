// SPDX-License-Identifier: GPL-3.0-or-later
// PC test of xdk/frontend/emu.cpp: starts a ROM through the same wrapper the
// Xbox frontend uses, runs 120 DS frames with 3 render threads, and checks
// that frames come out and input calls work. Built by test_core_pc.sh.
#include "emu.h"

#include <cstdio>

// Runs the ROM once; returns false on failure.
static bool runOnce(const char *rom, int round) {
    emu::Options options;
    options.romPath = rom;
    options.dataFolder = ".";
    options.biosFolder = ".";
    options.romInRam = true;
    options.renderThreads = 3;
    std::string error;
    if (!emu::start(options, &error)) { std::printf("FAIL start: %s\n", error.c_str()); return false; }

    static uint16_t frame[emu::kWidth * emu::kHeight];
    unsigned frames = 0, returns = 0;
    uint32_t hash = 2166136261u;
    while (emu::completedFrames() < 120 && returns < 100000) {
        if (!emu::run(&error)) { std::printf("FAIL run: %s\n", error.c_str()); return false; }
        returns++;
        emu::setInput(emu::kA, (returns & 1) != 0, 100, 80);
        if (emu::takeFrame(frame)) {
            frames++;
            for (int i = 0; i < emu::kWidth * emu::kHeight; i++)
                hash = (hash ^ frame[i]) * 16777619u;
        }
    }
    std::printf("%s emu wrapper round %d: completed=%u frames_taken=%u returns=%u last_hash=%08X %s\n",
        frames > 0 && emu::completedFrames() >= 120 ? "PASS" : "FAIL", round,
        emu::completedFrames(), frames, returns, hash, emu::diagnostics().c_str());
    emu::stop();
    return frames > 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { std::printf("usage: %s rom.nds\n", argv[0]); return 2; }
    // Twice: the menu stops a game and starts another in the same run.
    return runOnce(argv[1], 1) && runOnce(argv[1], 2) ? 0 : 1;
}
