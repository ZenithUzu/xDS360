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

#include <cstdint>
#include <cstdio>
#include <mutex>

enum Language {
    LG_JAPANESE = 0,
    LG_ENGLISH,
    LG_FRENCH,
    LG_GERMAN,
    LG_ITALIAN,
    LG_SPANISH
};

class Core;

class Spi {
public:
    uint16_t touchX;
    uint16_t touchY;

    Spi(Core *core): core(core), touchX(0x000), touchY(0xFFF), firmware(nullptr), firmSize(0), micBuffer(nullptr), micBufSize(0), micCycles(0), micStep(0), micSample(0), writeCount(0), address(0), command(0), spiCnt(0), spiData(0) {}
    ~Spi();

    void saveState(FILE *file);
    void loadState(FILE *file);

    bool loadFirmware();
    void directBoot();

    void setTouch(int x, int y);
    void clearTouch();

    static void setLanguage(Language lang) { language = lang; }
    void sendMicData(const int16_t* samples, size_t count, size_t rate);

    uint16_t readSpiCnt() { return spiCnt; }
    uint8_t readSpiData() { return spiData; }

    void writeSpiCnt(uint16_t mask, uint16_t value);
    void writeSpiData(uint8_t value);

private:
    Core *core;

    static Language language;
    uint8_t *firmware;
    size_t firmSize;

    int16_t *micBuffer;
    size_t micBufSize;
    uint32_t micCycles;
    uint32_t micStep;
    uint16_t micSample;
    std::mutex mutex;

    uint32_t writeCount;
    uint32_t address;
    uint8_t command;

    uint16_t spiCnt;
    uint8_t spiData;

    static uint16_t crc16(uint32_t value, uint8_t *data, size_t size);
};
