# xDS360

🌐 **English** · [Español](README.md)

An experimental Nintendo DS emulator for Xbox 360 homebrew, rebuilt to compile
with **Visual Studio 2010 and the Xbox 360 SDK (XDK)** and run as a regular
`.xex` from Aurora and other homebrew dashboards.

xDS360 is a fork of [XenonDS](https://github.com/jmay46/XenonDS) by jmay46.
XenonDS proved that a patched [NooDS](https://github.com/Hydr8gon/NooDS) core can
boot retail DS games on real Xbox 360 hardware using LibXenon. This fork takes
that work as its foundation and moves the platform layer to the official SDK
toolchain, so the emulator can share the frontend, audio and library work
already done for [AdvanXe360](https://github.com/ZenithUzu/AdvanXe360).

> **Project status:** Early development. There is no xDS360 release yet. The
> code currently in this repository is the upstream XenonDS LibXenon port
> (v0.9.1), kept as the working reference while the XDK version is built.

## Goals

What we expect xDS360 to become:

- **Native XDK build.** A Visual Studio 2010 solution that produces a `.xex`,
  launched from Aurora/FreeStyle like any other homebrew. No XeLL required.
- **Works on modern exploits.** Targets RGH/JTAG consoles and BadUpdate, and is
  tested on real hardware first, with Xenia as a secondary check.
- **NooDS core with the XenonDS Xbox patches.** Big-endian memory access, the
  direct THUMB/branch dispatch, the sorted event queue and the multithreaded
  software 3D renderer all carry over.
- **Direct-boot by default.** No Nintendo BIOS or firmware files required.
- **Both screens with Direct3D.** Vertical, horizontal and single-screen
  layouts, integer and filtered scaling, and picture controls.
- **Audio through XAudio2.** Stereo DS sound, which the LibXenon build does not
  have yet.
- **XInput controls.** Full DS button mapping, with the right stick acting as the
  touch-screen cursor.
- **A real ROM library.** A browser with covers and themes, built on
  [Apariencia-Emulador](https://github.com/ZenithUzu/Apariencia-Emulador),
  instead of a single `game.nds` file.
- **Saves and save states.** Persistent cartridge `.sav` files from the start,
  save states later.
- **Honest performance numbers.** An on-screen counter that separates emulated
  frames, displayed frames and real DS speed.

### Performance expectations

The Nintendo DS is far heavier to emulate than the Game Boy Advance, and the
Xbox 360 CPU struggles with interpreter-style code. On real hardware, the
upstream LibXenon build reached about 22–23 FPS in the 3D-heavy scenes of
Pokémon Black with three rendering threads. Full speed in demanding 3D games
will most likely need an ARM-to-PowerPC recompiler, which is a long-term goal,
not a promise.

## Roadmap

1. **VS2010 compatibility layer.** XDK-backed replacements for `std::thread`,
   `std::mutex`, `std::condition_variable`, `std::atomic` and `std::chrono`;
   moving NooDS's in-class member initializers into constructors; MSVC
   replacements for the GCC builtins. Goal: the NooDS core compiles as a static
   library in VS2010.
2. **First frame on the console.** A minimal XDK frontend that loads a ROM,
   shows both screens and reports FPS.
3. **Playable.** XAudio2 audio, full controller and touch input, cartridge
   saves.
4. **Frontend.** ROM library, settings menus, screen layouts and filters.
5. **Speed.** Profiling on hardware and, eventually, a dynamic recompiler.

## Building

The XDK build is not in the repository yet. When it lands, building it will
require Visual Studio 2010 and your own licensed copy of the Xbox 360 SDK; no
Microsoft SDK files are, or will be, included here.

The inherited upstream builds still work as described in their own docs:

- Host tests and tools: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- LibXenon NooDS build: `scripts/build_xenon_noods.ps1` (see
  [Xbox 360 runtime testing](docs/XBOX_TESTING.md))
- Upstream sources: `scripts/fetch_noods.sh` checks out the pinned NooDS commit
  from `upstream.lock` and applies `patches/noods-xenon.patch`.

## Documentation

- [Architecture](docs/ARCHITECTURE.md) (upstream XenonDS)
- [Roadmap](docs/ROADMAP.md) (upstream XenonDS)
- [Xbox 360 runtime testing](docs/XBOX_TESTING.md) (upstream XenonDS)
- [Contributing](CONTRIBUTING.md)

## Credits

xDS360 exists because of the work of these projects and people:

- **[XenonDS](https://github.com/jmay46/XenonDS)** by jmay46: the original Xbox
  360 port, the NooDS Xbox compatibility patch, the multithreaded 3D renderer
  and the hardware testing this fork builds on.
- **[NooDS](https://github.com/Hydr8gon/NooDS)** by Hydr8gon: the Nintendo DS
  emulation core.
- **[DeSmuME](https://github.com/TASEmulators/desmume)** by the DeSmuME team:
  the core used by XenonDS's earlier checkpoints.
- **[LibXenon](https://github.com/X360Tools/libxenon)** and the Free60 project:
  the open-source Xbox 360 toolchain used by the upstream port.

## Legal

xDS360 does not include games, Nintendo BIOS or firmware files, encryption keys,
Microsoft SDK files, or proprietary Xbox software. Use ROM images and firmware
dumped from hardware and games you own.

The legacy DeSmuME executable is GPL-2.0; the NooDS executable is
GPL-3.0-or-later. Shared XenonDS code is GPL-2.0-or-later. Derived releases must
preserve the applicable license and provide corresponding source code.
