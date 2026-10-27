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
#include <deque>

class Core;

class SdMmc {
public:
    SdMmc(Core *core): core(core), nand(nullptr), sd(nullptr), sdhc(false), cardStatus(0), opCond(0x80000000), blockLen(0), curAddress(0), curBlock(0), sdCmd(0), sdPortSelect(0x200), sdCmdParam(0), sdData16Blkcnt(0), sdResponse(), sdIrqStatus(0xA0), sdIrqMask(0), sdData16Blklen(0), sdErrDetail(0), sdData16Fifo(0), sdDataCtl(0x1010), sdData32Irq(0), sdData32Blklen(0), sdData32Fifo(0), consoleId(), mmcCid() {}
    ~SdMmc();
    void saveState(FILE *file);
    void loadState(FILE *file);

    uint32_t *init();
    void readBlock();
    void writeBlock();

    uint16_t readCmd() { return sdCmd; }
    uint16_t readPortSelect() { return sdPortSelect; }
    uint32_t readCmdParam() { return sdCmdParam; }
    uint16_t readData16Blkcnt() { return sdData16Blkcnt; }
    uint32_t readResponse(int i) { return sdResponse[i]; }
    uint32_t readIrqStatus() { return sdIrqStatus; }
    uint32_t readIrqMask() { return sdIrqMask; }
    uint16_t readData16Blklen() { return sdData16Blklen; }
    uint32_t readErrDetail() { return sdErrDetail; }
    uint16_t readData16Fifo();
    uint16_t readDataCtl() { return sdDataCtl; }
    uint16_t readData32Irq() { return sdData32Irq; }
    uint16_t readData32Blklen() { return sdData32Blklen; }
    uint32_t readData32Fifo();
    uint32_t readConsoleId(int i) { return consoleId[i]; }

    void writeCmd(uint16_t mask, uint16_t value);
    void writePortSelect(uint16_t mask, uint16_t value);
    void writeCmdParam(uint32_t mask, uint32_t value);
    void writeData16Blkcnt(uint16_t mask, uint16_t value);
    void writeIrqStatus(uint32_t mask, uint32_t value);
    void writeIrqMask(uint32_t mask, uint32_t value);
    void writeData16Blklen(uint16_t mask, uint16_t value);
    void writeData16Fifo(uint16_t mask, uint16_t value);
    void writeDataCtl(uint16_t mask, uint16_t value);
    void writeData32Irq(uint16_t mask, uint16_t value);
    void writeData32Blklen(uint16_t mask, uint16_t value);
    void writeData32Fifo(uint32_t mask, uint32_t value);

private:
    Core *core;

    FILE *nand;
    FILE *sd;
    bool sdhc;

    uint32_t cardStatus;
    uint32_t opCond;
    uint32_t blockLen;
    uint32_t curAddress;
    uint16_t curBlock;

    std::deque<uint16_t> dataFifo16;
    std::deque<uint32_t> dataFifo32;

    uint16_t sdCmd;
    uint16_t sdPortSelect;
    uint32_t sdCmdParam;
    uint16_t sdData16Blkcnt;
    uint32_t sdResponse[4];
    uint32_t sdIrqStatus;
    uint32_t sdIrqMask;
    uint16_t sdData16Blklen;
    uint32_t sdErrDetail;
    uint16_t sdData16Fifo;
    uint32_t sdDataCtl;
    uint16_t sdData32Irq;
    uint32_t sdData32Blklen;
    uint32_t sdData32Fifo;

    uint32_t consoleId[0x2];
    uint32_t mmcCid[0x4];

    void sendInterrupt(int bit);
    void startWrite();
    uint32_t popFifo();
    void pushFifo(uint32_t value);
    void pushResponse(uint32_t value);
    void runCommand();
    void runAppCommand();

    void setIfCond();
    void getCid();
    void getCsd();
    void getStatus();
    void setBlocklen();
    void readSingleBlock();
    void readMultiBlock();
    void writeSingleBlock();
    void writeMultiBlock();
    void appCmd();
    void sdStatus();
    void sdSendOpCond();
    void getScr();
};
