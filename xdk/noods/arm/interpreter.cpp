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

#include "../core.h"

namespace {

FORCE_INLINE bool continueCore(Core &core) {
#ifdef NOODS_XENON
    // Xbox emulation owns Core::running on this hardware thread. Relaxed
    // ordering preserves the atomic state transition without paying for a
    // full PowerPC memory barrier at every scheduler event.
    return core.running.exchange(true, std::memory_order_relaxed);
#else
    return core.running.exchange(true);
#endif
}

FORCE_INLINE uint32_t nextEventCycles(Core &core) {
#ifdef NOODS_XENON
    return core.events.back().cycles;
#else
    return core.events.front().cycles;
#endif
}

FORCE_INLINE void runScheduledEvents(Core &core) {
    core.globalCycles = nextEventCycles(core);
    while (nextEventCycles(core) <= core.globalCycles) {
#ifdef NOODS_XENON
        // Xenon's queue is reverse-sorted, so removing the earliest event is
        // O(1). Pop before invoking it because callbacks may schedule events.
        const SchedTask task = core.events.back().task;
        core.events.pop_back();
        core.runTask(task);
#else
        core.tasks[core.events.front().task]();
        core.events.erase(core.events.begin());
#endif
    }
}

} // namespace

Interpreter::Interpreter(Core *core, bool arm7): core(core), arm7(arm7), bios(nullptr), entryAddr(0), halted(0), unknownOpcodeCount(0), lastUnknownOpcode(0), pcData(nullptr), pipeline(), registers(), registersUsr(), registersFiq(), registersSvc(), registersAbt(), registersIrq(), registersUnd(), cpsr(0), spsr(nullptr), spsrFiq(0), spsrSvc(0), spsrAbt(0), spsrIrq(0), spsrUnd(0), cycles(0), dsiCycle(false), ime(0), ie(0), irf(0), postFlg(0) {
    // Initialize the registers for user mode
    for (int i = 0; i < 32; i++)
        registers[i] = &registersUsr[i & 0xF];
}

void Interpreter::saveState(FILE *file) {
    // Write state data to the file
    fwrite(pipeline, 4, sizeof(pipeline) / 4, file);
    fwrite(registersUsr, 4, sizeof(registersUsr) / 4, file);
    fwrite(registersFiq, 4, sizeof(registersFiq) / 4, file);
    fwrite(registersSvc, 4, sizeof(registersSvc) / 4, file);
    fwrite(registersAbt, 4, sizeof(registersAbt) / 4, file);
    fwrite(registersIrq, 4, sizeof(registersIrq) / 4, file);
    fwrite(registersUnd, 4, sizeof(registersUnd) / 4, file);
    fwrite(&cpsr, sizeof(cpsr), 1, file);
    fwrite(&spsrFiq, sizeof(spsrFiq), 1, file);
    fwrite(&spsrSvc, sizeof(spsrSvc), 1, file);
    fwrite(&spsrAbt, sizeof(spsrAbt), 1, file);
    fwrite(&spsrIrq, sizeof(spsrIrq), 1, file);
    fwrite(&spsrUnd, sizeof(spsrUnd), 1, file);
    fwrite(&cycles, sizeof(cycles), 1, file);
    fwrite(&halted, sizeof(halted), 1, file);
    fwrite(&dsiCycle, sizeof(dsiCycle), 1, file);
    fwrite(&ime, sizeof(ime), 1, file);
    fwrite(&ie, sizeof(ie), 1, file);
    fwrite(&irf, sizeof(irf), 1, file);
    fwrite(&postFlg, sizeof(postFlg), 1, file);
}

void Interpreter::loadState(FILE *file) {
    // Read state data from the file
    fread(pipeline, 4, sizeof(pipeline) / 4, file);
    fread(registersUsr, 4, sizeof(registersUsr) / 4, file);
    fread(registersFiq, 4, sizeof(registersFiq) / 4, file);
    fread(registersSvc, 4, sizeof(registersSvc) / 4, file);
    fread(registersAbt, 4, sizeof(registersAbt) / 4, file);
    fread(registersIrq, 4, sizeof(registersIrq) / 4, file);
    fread(registersUnd, 4, sizeof(registersUnd) / 4, file);
    fread(&cpsr, sizeof(cpsr), 1, file);
    fread(&spsrFiq, sizeof(spsrFiq), 1, file);
    fread(&spsrSvc, sizeof(spsrSvc), 1, file);
    fread(&spsrAbt, sizeof(spsrAbt), 1, file);
    fread(&spsrIrq, sizeof(spsrIrq), 1, file);
    fread(&spsrUnd, sizeof(spsrUnd), 1, file);
    fread(&cycles, sizeof(cycles), 1, file);
    fread(&halted, sizeof(halted), 1, file);
    fread(&dsiCycle, sizeof(dsiCycle), 1, file);
    fread(&ime, sizeof(ime), 1, file);
    fread(&ie, sizeof(ie), 1, file);
    fread(&irf, sizeof(irf), 1, file);
    fread(&postFlg, sizeof(postFlg), 1, file);

    // Update mapped registers
    swapRegisters(cpsr);
    pcData = nullptr;
}

void Interpreter::init() {
    // Prepare to boot the BIOS
    setCpsr(0x000000D3); // Supervisor, interrupts off
    registersUsr[15] = arm7 ? 0x00000000 : 0xFFFF0000;
    flushPipeline();

    // Reset the I/O registers
    ime = 0;
    ie = irf = 0;
    postFlg = 0;
}

void Interpreter::directBoot() {
    // Prepare to directly boot a ROM
    setCpsr(0x000000DF); // System, interrupts off
    registersUsr[15] = core->gbaMode ? 0x8000000 : entryAddr;
    registersUsr[14] = registersUsr[15];
    registersUsr[12] = registersUsr[15];
    registersUsr[13] = arm7 ? (core->gbaMode ? 0x3007F00 : 0x380FD80) : 0x3002F7C;
    registersIrq[0] = arm7 ? (core->gbaMode ? 0x3007FA0 : 0x380FF80) : 0x3003F80;
    registersSvc[0] = arm7 ? (core->gbaMode ? 0x3007FE0 : 0x380FFC0) : 0x3003FC0;
    flushPipeline();
}

void Interpreter::resetCycles() {
    // Adjust CPU cycles for a global cycle reset
    cycles -= std::min(core->globalCycles, cycles);
}

void Interpreter::runCoreNone(Core &core) {
    // Run the core with no active CPUs
    while (continueCore(core)) {
        // Jump to the next task and run all that are scheduled now
        runScheduledEvents(core);
    }
}

template void Interpreter::runCoreSingle<false, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 1>(Core &core);
template <bool _arm7, int shift> void Interpreter::runCoreSingle(Core &core) {
    // Run the core with one active CPU
    Interpreter &arm = core.interpreter[_arm7];
    while (continueCore(core)) {
        // Run a CPU until the next scheduled task
        core.globalCycles = std::max(core.globalCycles, arm.cycles);
        while (nextEventCycles(core) > arm.cycles)
            arm.cycles = (core.globalCycles += arm.runOpcode() << shift);

        // Jump to the next task and run all that are scheduled now
        runScheduledEvents(core);
    }
}

void Interpreter::runCoreNds(Core &core) {
    // Run the core with both CPUs active in NDS mode
    Interpreter &arm9 = core.interpreter[0];
    Interpreter &arm7 = core.interpreter[1];
    while (continueCore(core)) {
        // Run the ARM9 and half-speed ARM7 until the next scheduled task
        while (nextEventCycles(core) > core.globalCycles) {
            if (core.globalCycles >= arm9.cycles)
                arm9.cycles = core.globalCycles + arm9.runOpcode();
            if (core.globalCycles >= arm7.cycles)
                arm7.cycles = core.globalCycles + (arm7.runOpcode() << 1);
            core.globalCycles = std::min<uint32_t>(arm9.cycles, arm7.cycles);
        }

        // Jump to the next task and run all that are scheduled now
        runScheduledEvents(core);
    }
}

void Interpreter::runCoreDsi(Core &core) {
    // Run the core in DSi mode
    Interpreter &arm9 = core.interpreter[0];
    Interpreter &arm7 = core.interpreter[1];
    while (continueCore(core)) {
        // Run both CPUs until the next scheduled task
        while (nextEventCycles(core) > core.globalCycles) {
            // Run the ARM9 twice as fast as usual
            if (core.globalCycles >= arm9.cycles) {
                int cycles = arm9.runOpcode() + arm9.dsiCycle;
                arm9.cycles = core.globalCycles + (cycles >> 1);
                arm9.dsiCycle = (cycles & 0x1);
            }

            // Run the ARM7 at half speed and advance to the next opcode cycle
            if (core.globalCycles >= arm7.cycles)
                arm7.cycles = core.globalCycles + (arm7.runOpcode() << 1);
            core.globalCycles = std::min<uint32_t>(arm9.cycles, arm7.cycles);
        }

        // Jump to the next task and run all that are scheduled now
        runScheduledEvents(core);
    }
}

FORCE_INLINE int Interpreter::runOpcode() {
    // Push the next opcode through the pipeline
    uint32_t opcode = pipeline[0];
    pipeline[0] = pipeline[1];

    // Execute an instruction
    if (cpsr & BIT(5)) { // THUMB mode
        // Increment the program counter and fill the pipeline from pointer or fallback
        pipeline[1] = (((registersUsr[15] += 2) & 0xFFE) && pcData) ?
            readLe16Aligned(pcData += 2) : getOpcode16();

        // Execute a THUMB instruction through a branch-friendly decoder. The
        // stock member-pointer table is compact, but its indirect call is very
        // expensive on Xenon's in-order PowerPC cores.
#ifdef NOODS_XENON
        return runThumbOpcode(opcode);
#else
        return (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
#endif
    }
    else { // ARM mode
        // Increment the program counter and fill the pipeline from pointer or fallback
        pipeline[1] = (((registersUsr[15] += 4) & 0xFFC) && pcData) ?
            readLe32Aligned(pcData += 4) : getOpcode32();

        // Execute an ARM instruction based on its condition
        switch (condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]) {
            case 0: return 1; // False
            case 2: return handleReserved(opcode); // Reserved
            default:
#ifdef NOODS_XENON
                // ARM branches dominate tight game loops. Bypass the generic
                // member-pointer table for branch, branch-with-link, and SWI.
                switch (opcode & 0x0F000000u) {
                case 0x0A000000u: return b(opcode);
                case 0x0B000000u: return bl(opcode);
                case 0x0F000000u: return swi(opcode);
                default: break;
                }
#endif
                return (this->*armInstrs[((opcode >> 16) & 0xFF0) |
                                         ((opcode >> 4) & 0xF)])(opcode);
        }
    }
}

FORCE_INLINE int Interpreter::runThumbOpcode(uint16_t opcode) {
    const unsigned int index = opcode >> 6;

    // Most THUMB encodings occupy contiguous 32-entry groups. Decoding those
    // groups with a small switch turns an unpredictable member-pointer call
    // into direct, inlineable calls while preserving the exact NooDS handlers.
    switch (index >> 5) {
    case 0x00: return lslImmT(opcode);
    case 0x01: return lsrImmT(opcode);
    case 0x02: return asrImmT(opcode);
    case 0x03:
        switch ((index >> 3) & 0x3) {
        case 0: return addRegT(opcode);
        case 1: return subRegT(opcode);
        case 2: return addImm3T(opcode);
        default: return subImm3T(opcode);
        }
    case 0x04: return movImm8T(opcode);
    case 0x05: return cmpImm8T(opcode);
    case 0x06: return addImm8T(opcode);
    case 0x07: return subImm8T(opcode);
    case 0x09: return ldrPcT(opcode);
    case 0x0A:
    case 0x0B:
        switch (index >> 3) {
        case 0x28: return strRegT(opcode);
        case 0x29: return strhRegT(opcode);
        case 0x2A: return strbRegT(opcode);
        case 0x2B: return ldrsbRegT(opcode);
        case 0x2C: return ldrRegT(opcode);
        case 0x2D: return ldrhRegT(opcode);
        case 0x2E: return ldrbRegT(opcode);
        default: return ldrshRegT(opcode);
        }
    case 0x0C: return strImm5T(opcode);
    case 0x0D: return ldrImm5T(opcode);
    case 0x0E: return strbImm5T(opcode);
    case 0x0F: return ldrbImm5T(opcode);
    case 0x10: return strhImm5T(opcode);
    case 0x11: return ldrhImm5T(opcode);
    case 0x12: return strSpT(opcode);
    case 0x13: return ldrSpT(opcode);
    case 0x14: return addPcT(opcode);
    case 0x15: return addSpT(opcode);
    case 0x18: return stmiaT(opcode);
    case 0x19: return ldmiaT(opcode);
    case 0x1A:
    case 0x1B:
        switch (index >> 2) {
        case 0xD0: return beqT(opcode);
        case 0xD1: return bneT(opcode);
        case 0xD2: return bcsT(opcode);
        case 0xD3: return bccT(opcode);
        case 0xD4: return bmiT(opcode);
        case 0xD5: return bplT(opcode);
        case 0xD6: return bvsT(opcode);
        case 0xD7: return bvcT(opcode);
        case 0xD8: return bhiT(opcode);
        case 0xD9: return blsT(opcode);
        case 0xDA: return bgeT(opcode);
        case 0xDB: return bltT(opcode);
        case 0xDC: return bgtT(opcode);
        case 0xDD: return bleT(opcode);
        case 0xDF: return swiT(opcode);
        default: return unkThumb(opcode);
        }
    case 0x1C: return bT(opcode);
    case 0x1D: return blxOffT(opcode);
    case 0x1E: return blSetupT(opcode);
    case 0x1F: return blOffT(opcode);
    default: break;
    }

    // Mixed groups are still direct-decoded where doing so stays compact.
    if (index >= 0x110 && index <= 0x113) return addHT(opcode);
    if (index >= 0x114 && index <= 0x117) return cmpHT(opcode);
    if (index >= 0x118 && index <= 0x11B) return movHT(opcode);
    if (index >= 0x11C && index <= 0x11D) return bxRegT(opcode);
    if (index >= 0x11E && index <= 0x11F) return blxRegT(opcode);
    if (index >= 0x2C0 && index <= 0x2C3) return addSpImmT(opcode);
    if (index >= 0x2D0 && index <= 0x2D3) return pushT(opcode);
    if (index >= 0x2D4 && index <= 0x2D7) return pushLrT(opcode);
    if (index >= 0x2F0 && index <= 0x2F3) return popT(opcode);
    if (index >= 0x2F4 && index <= 0x2F7) return popPcT(opcode);

    switch (index) {
    case 0x100: return andDpT(opcode);
    case 0x101: return eorDpT(opcode);
    case 0x102: return lslDpT(opcode);
    case 0x103: return lsrDpT(opcode);
    case 0x104: return asrDpT(opcode);
    case 0x105: return adcDpT(opcode);
    case 0x106: return sbcDpT(opcode);
    case 0x107: return rorDpT(opcode);
    case 0x108: return tstDpT(opcode);
    case 0x109: return negDpT(opcode);
    case 0x10A: return cmpDpT(opcode);
    case 0x10B: return cmnDpT(opcode);
    case 0x10C: return orrDpT(opcode);
    case 0x10D: return mulDpT(opcode);
    case 0x10E: return bicDpT(opcode);
    case 0x10F: return mvnDpT(opcode);
    default: return (this->*thumbInstrs[index])(opcode);
    }
}

uint16_t Interpreter::getOpcode16() {
    // Set the opcode pointer or fall back to a regular 16-bit opcode read
    if (!(pcData = (arm7 ? core->memory.readMap7 : core->memory.readMap9A)[registersUsr[15] >> 12]))
        return core->memory.read<uint16_t>(arm7, registersUsr[15]);
    pcData += (registersUsr[15] & 0xFFE);
    return readLe16Aligned(pcData);
}

uint32_t Interpreter::getOpcode32() {
    // Set the opcode pointer or fall back to a regular 32-bit opcode read
    if (!(pcData = (arm7 ? core->memory.readMap7 : core->memory.readMap9A)[registersUsr[15] >> 12]))
        return core->memory.read<uint32_t>(arm7, registersUsr[15]);
    pcData += (registersUsr[15] & 0xFFC);
    return readLe32Aligned(pcData);
}

void Interpreter::halt(int bit) {
    // Set a halt bit and disable the CPU if newly halted
    bool before = halted;
    halted |= BIT(bit);
    if (before) return;
    core->schedule(UPDATE_RUN, 0);
    cycles = 0xFFFFFFFF;
}

void Interpreter::unhalt(int bit) {
    // Clear a halt bit and enable the CPU if newly halted
    bool before = halted;
    halted &= ~BIT(bit);
    if (!before) return;
    core->schedule(UPDATE_RUN, 0);
    cycles = 0;
}

void Interpreter::sendInterrupt(int bit) {
    // Set the interrupt's request bit
    irf |= BITL(bit);

    // Trigger an interrupt if the conditions are met, or unhalt the CPU even if interrupts are disabled
    // The ARM9 additionally needs IME to be set for it to unhalt, but the ARM7 doesn't care
    if (ie & irf) {
        if (ime && !(cpsr & BIT(7)))
            core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
        else if (ime || arm7)
            unhalt(0);
    }
}

void Interpreter::interrupt() {
    // Trigger an interrupt and unhalt the CPU if the conditions still hold
    if (ime && (ie & irf) && !(cpsr & BIT(7))) {
        exception(0x18);
        unhalt(0);
    }
}

int Interpreter::exception(uint8_t vector) {
    // Forward the call to HLE BIOS if enabled, unless on ARM9 with the exception address changed
    if (bios && (arm7 || core->cp15.exceptionAddr))
        return bios->execute(vector, registers);

    // Switch the CPU mode, save the return address, and jump to the exception vector
    static const uint8_t modes[] = { 0x13, 0x1B, 0x13, 0x17, 0x17, 0x13, 0x12, 0x11 };
    setCpsr((cpsr & ~0x3F) | BIT(7) | modes[vector >> 2], true); // ARM, interrupts off, new mode
    *registers[14] = registersUsr[15] + ((*spsr & BIT(5)) >> 4);
    registersUsr[15] = (arm7 ? 0 : core->cp15.exceptionAddr) + vector;
    flushPipeline();
    return 3;
}

void Interpreter::flushPipeline() {
    // Adjust the program counter and refill the pipeline after a jump
    if (cpsr & BIT(5)) { // THUMB mode
        registersUsr[15] = (registersUsr[15] & ~0x1) + 2;
        pipeline[0] = core->memory.read<uint16_t>(arm7, registersUsr[15] - 2);
        pipeline[1] = getOpcode16();
    }
    else { // ARM mode
        registersUsr[15] = (registersUsr[15] & ~0x3) + 4;
        pipeline[0] = core->memory.read<uint32_t>(arm7, registersUsr[15] - 4);
        pipeline[1] = getOpcode32();
    }
}

void Interpreter::swapRegisters(uint32_t value) {
    // Swap banked registers based on a CPU mode value
    switch (value & 0x1F) {
    case 0x10: // User
    case 0x1F: // System
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersUsr[13];
        registers[14] = &registersUsr[14];
        spsr = nullptr;
        break;

    case 0x11: // FIQ
        registers[8] = &registersFiq[0];
        registers[9] = &registersFiq[1];
        registers[10] = &registersFiq[2];
        registers[11] = &registersFiq[3];
        registers[12] = &registersFiq[4];
        registers[13] = &registersFiq[5];
        registers[14] = &registersFiq[6];
        spsr = &spsrFiq;
        break;

    case 0x12: // IRQ
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersIrq[0];
        registers[14] = &registersIrq[1];
        spsr = &spsrIrq;
        break;

    case 0x13: // Supervisor
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersSvc[0];
        registers[14] = &registersSvc[1];
        spsr = &spsrSvc;
        break;

    case 0x17: // Abort
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersAbt[0];
        registers[14] = &registersAbt[1];
        spsr = &spsrAbt;
        break;

    case 0x1B: // Undefined
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersUnd[0];
        registers[14] = &registersUnd[1];
        spsr = &spsrUnd;
        break;

    default:
        LOG_CRIT("Unknown ARM%d CPU mode: 0x%X\n", arm7 ? 7 : 9, value & 0x1F);
        break;
    }
}

void Interpreter::setCpsr(uint32_t value, bool save) {
    // Update registers if the CPU mode changed
    if ((value & 0x1F) != (cpsr & 0x1F))
        swapRegisters(value);

    // Set the CPSR, saving the old value if requested
    if (save && spsr) *spsr = cpsr;
    cpsr = value;

    // Trigger an interrupt if the conditions are met
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

int Interpreter::handleReserved(uint32_t opcode) {
    // The ARM9-exclusive BLX instruction uses the reserved condition code, so let it run
    if ((opcode & 0xE000000) == 0xA000000)
        return blx(opcode); // BLX label

    // If the special HLE BIOS opcode was jumped to, return from an HLE interrupt
    if (bios && opcode == 0xFF000000)
        return finishHleIrq();

    // If a DLDI function was jumped to, HLE it and return
    if (core->dldi.isPatched()) {
        uint32_t **r = registers;
        switch (opcode) {
            case DLDI_START: *r[0] = core->dldi.startup(); break;
            case DLDI_INSERT: *r[0] = core->dldi.isInserted(); break;
            case DLDI_READ: *r[0] = core->dldi.readSectors(arm7, *r[0], *r[1], *r[2]); break;
            case DLDI_WRITE: *r[0] = core->dldi.writeSectors(arm7, *r[0], *r[1], *r[2]); break;
            case DLDI_CLEAR: *r[0] = core->dldi.clearStatus(); break;
            case DLDI_STOP: *r[0] = core->dldi.shutdown(); break;
        }
        return bx(14);
    }

    // Treat anything else as an unknown opcode
    return unkArm(opcode);
}

int Interpreter::handleHleIrq() {
    // Switch to IRQ mode, save the return address, and push registers to the stack
    setCpsr((cpsr & ~0x3F) | BIT(7) | 0x12, true);
    *registers[14] = registersUsr[15] + ((*spsr & BIT(5)) ? 2 : 0);
    stmdbW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));

    // Set the return address to the special HLE BIOS opcode amd jump to the interrupt handler
    *registers[14] = arm7 ? 0x00000000 : 0xFFFF0000;
    registersUsr[15] = core->memory.read<uint32_t>(arm7, arm7 ? 0x3FFFFFC : (core->cp15.dtcmAddr + 0x3FFC));
    flushPipeline();
    return 3;
}

int Interpreter::finishHleIrq() {
    // Update the wait flags if in the middle of an HLE IntrWait function
    if (bios->shouldCheck())
        bios->checkWaitFlags();

    // Pop registers from the stack, jump to the return address, and restore the mode
    ldmiaW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));
    registersUsr[15] = *registers[14] - 4;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

int Interpreter::unkArm(uint32_t opcode) {
    // Handle an unknown ARM opcode
    unknownOpcodeCount++;
    lastUnknownOpcode = opcode;
    LOG_CRIT("Unknown ARM%d ARM opcode: 0x%X\n", arm7 ? 7 : 9, opcode);
    return 1;
}

int Interpreter::unkThumb(uint16_t opcode) {
    // Handle an unknown THUMB opcode
    unknownOpcodeCount++;
    lastUnknownOpcode = opcode;
    LOG_CRIT("Unknown ARM%d THUMB opcode: 0x%X\n", arm7 ? 7 : 9, opcode);
    return 1;
}

void Interpreter::writeIme(uint8_t value) {
    // Write to the IME register
    ime = value & 0x01;

    // Trigger an interrupt if the conditions are met
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

void Interpreter::writeIe(int i, uint32_t mask, uint32_t value) {
    // Write to one of the IE registers
    uint64_t mask2 = (uint64_t(mask) << (i * 32)) & (arm7 ? (core->gbaMode ? 0x3FFF : 0x7FF7FFFFFFFF) : 0xFFFFFF7F);
    ie = (ie & ~mask2) | ((uint64_t(value) << (i * 32)) & mask2);

    // Trigger an interrupt if the conditions are met
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

void Interpreter::writeIrf(int i, uint32_t mask, uint32_t value) {
    // Clear bits in one of the IF registers
    irf &= ~(uint64_t(value & mask) << (i * 32));
}

void Interpreter::writePostFlg(uint8_t value) {
    // Write to the unclearable POSTFLG bit (with an extra writable bit on ARM9)
    postFlg |= value & 0x01;
    if (!arm7) postFlg = (postFlg & ~0x02) | (value & 0x02);
}
