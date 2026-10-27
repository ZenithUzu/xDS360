# xDS360

**English** · [Español](#español)

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

---

# Español

[English](#xds360) · **Español**

Un emulador experimental de Nintendo DS para Xbox 360 homebrew, rehecho para
compilarse con **Visual Studio 2010 y el SDK de Xbox 360 (XDK)** y abrirse como un
`.xex` normal desde Aurora y otros dashboards homebrew.

xDS360 es un fork de [XenonDS](https://github.com/jmay46/XenonDS), de jmay46.
XenonDS demostró que un núcleo [NooDS](https://github.com/Hydr8gon/NooDS) con
parches puede arrancar juegos comerciales de DS en una Xbox 360 real usando
LibXenon. Este fork toma ese trabajo como base y pasa la parte de la consola a las
herramientas oficiales del SDK, para aprovechar la interfaz, el audio y la
biblioteca que ya hicimos en [AdvanXe360](https://github.com/ZenithUzu/AdvanXe360).

> **Estado:** desarrollo temprano. Todavía no hay ningún release de xDS360. El
> código que hay ahora en el repositorio es el port original de XenonDS con
> LibXenon (v0.9.1), que se conserva como referencia mientras se hace la versión
> con el XDK.

## Objetivos

Lo que esperamos que sea xDS360:

- **Compilación nativa con el XDK.** Una solución de Visual Studio 2010 que genere
  un `.xex` y se abra desde Aurora/FreeStyle como cualquier homebrew. Sin XeLL.
- **Funciona con los exploits actuales.** Para consolas RGH/JTAG y BadUpdate. Se
  prueba primero en consola real y después en Xenia.
- **Núcleo NooDS con los parches de Xbox de XenonDS.** Se conservan el acceso a
  memoria big-endian, el despacho directo de THUMB y saltos, la cola de eventos
  ordenada y el render 3D por software en varios hilos.
- **Arranque directo por defecto.** No hacen falta la BIOS ni el firmware de
  Nintendo.
- **Las dos pantallas con Direct3D.** Vertical, horizontal o una sola pantalla,
  escalado entero o con filtro y ajustes de imagen.
- **Audio con XAudio2.** Sonido estéreo del DS, que la versión con LibXenon
  todavía no tiene.
- **Controles con XInput.** Todos los botones del DS, y el stick derecho como
  cursor de la pantalla táctil.
- **Una biblioteca de ROMs de verdad.** Un explorador con portadas y temas, hecho
  con [Apariencia-Emulador](https://github.com/ZenithUzu/Apariencia-Emulador), en
  lugar de un solo archivo `game.nds`.
- **Partidas guardadas y estados.** Archivos `.sav` del cartucho desde el inicio y
  estados de guardado más adelante.
- **Números de rendimiento honestos.** Un contador en pantalla que separa los
  cuadros emulados, los cuadros mostrados y la velocidad real del DS.

### Qué esperar del rendimiento

El Nintendo DS es mucho más pesado de emular que el Game Boy Advance, y el
procesador de la Xbox 360 sufre con código de tipo intérprete. En consola real, la
versión original con LibXenon llegó a unos 22–23 FPS en las escenas con mucho 3D
de Pokémon Black, usando tres hilos de render. Para ir a velocidad completa en
juegos 3D exigentes seguramente hará falta un recompilador de ARM a PowerPC. Esa
es una meta a largo plazo, no una promesa.

## Plan de trabajo

1. **Compatibilidad con VS2010.** Versiones propias, hechas con el XDK, de
   `std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic` y
   `std::chrono`; pasar los valores iniciales de los miembros de NooDS a los
   constructores; y reemplazos de MSVC para las funciones especiales de GCC.
   Meta: que el núcleo NooDS compile como librería estática en VS2010.
2. **Primer cuadro en la consola.** Un frontend mínimo con el XDK que cargue una
   ROM, muestre las dos pantallas y diga los FPS.
3. **Jugable.** Audio con XAudio2, control y pantalla táctil completos, partidas
   guardadas.
4. **Interfaz.** Biblioteca de ROMs, menús de opciones, distribución de pantallas
   y filtros.
5. **Velocidad.** Medir en la consola y, con el tiempo, un recompilador dinámico.

## Compilar

La versión con el XDK todavía no está en el repositorio. Cuando llegue, se
necesitará Visual Studio 2010 y tu propia copia con licencia del SDK de Xbox 360;
aquí no se incluye, ni se incluirá, ningún archivo del SDK de Microsoft.

Las compilaciones heredadas del proyecto original siguen funcionando como explica
su documentación (en inglés):

- Pruebas y herramientas en PC: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- Versión NooDS con LibXenon: `scripts/build_xenon_noods.ps1` (ver
  [Xbox 360 runtime testing](docs/XBOX_TESTING.md))
- Código original: `scripts/fetch_noods.sh` descarga el commit de NooDS fijado en
  `upstream.lock` y aplica `patches/noods-xenon.patch`.

## Créditos

xDS360 existe gracias al trabajo de estos proyectos y personas:

- **[XenonDS](https://github.com/jmay46/XenonDS)**, de jmay46: el port original a
  Xbox 360, el parche de compatibilidad de NooDS, el render 3D en varios hilos y
  las pruebas en consola sobre las que se construye este fork.
- **[NooDS](https://github.com/Hydr8gon/NooDS)**, de Hydr8gon: el núcleo de
  emulación de Nintendo DS.
- **[DeSmuME](https://github.com/TASEmulators/desmume)**, del equipo de DeSmuME:
  el núcleo de las primeras versiones de XenonDS.
- **[LibXenon](https://github.com/X360Tools/libxenon)** y el proyecto Free60: las
  herramientas libres de Xbox 360 que usa el port original.

## Aviso legal

xDS360 no incluye juegos, BIOS ni firmware de Nintendo, claves de cifrado,
archivos del SDK de Microsoft ni software propietario de Xbox. Usa ROMs y firmware
copiados de consolas y juegos que tengas.

El ejecutable heredado de DeSmuME es GPL-2.0; el de NooDS es GPL-3.0 o posterior.
El código compartido de XenonDS es GPL-2.0 o posterior. Las versiones derivadas
deben conservar la licencia correspondiente y publicar su código fuente.
