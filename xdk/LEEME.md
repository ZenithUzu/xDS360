# xDS360 con VS2010 y el XDK

Versión en desarrollo: un explorador simple para elegir la ROM, las dos
pantallas y los FPS. Todavía no tiene sonido ni opciones.

## Compilar

1. Abre `xdk/xds360.sln` con Visual Studio 2010 (con el XDK de Xbox 360 instalado).
2. Elige **Release | Xbox 360** y dale a **Build**.
3. El ejecutable sale en `xdk/Release/default.xex`.

## Poner en la consola

Copia solo `default.xex` a una carpeta de la consola. Al abrirlo crea solo:

```
(carpeta del emulador)\
    default.xex
    roms\       <- pon aquí tus .nds (también puedes usar subcarpetas)
    saves\      <- partidas guardadas (juego.sav), cada 5 segundos y al salir
    bios\       <- opcional: bios7.bin, bios9.bin, firmware.bin
    xds360.cfg  <- recuerda la última carpeta abierta
    xds360.log
```

- Los nombres de archivo deben tener **máximo 42 caracteres** (límite de FATX).
- Sin BIOS usa arranque directo.

## Explorador

Abre en la última carpeta que usaste (la primera vez, en `roms\`). Muestra
carpetas `[entre corchetes]` y archivos `.nds` con su tamaño.

| Botón | Qué hace |
|---|---|
| Cruceta o stick izquierdo | Moverse (mantener para avanzar rápido) |
| LB / RB | Página arriba / abajo |
| A | Abrir carpeta o jugar |
| B | Subir una carpeta; desde la raíz de una unidad, ver las unidades |
| Y | Ir a `roms\` |
| Back | Salir al dashboard |

Unidades: carpeta del emulador (`game:\`), disco duro (`hdd:\`) y USB
(`usb0:\` a `usb2:\`). Solo aparecen las que estén conectadas.

## Controles

| Xbox | DS |
|---|---|
| A, B, X, Y | A, B, X, Y |
| LB, RB | L, R |
| Start, Back | Start, Select |
| Cruceta | Cruceta |
| Stick derecho | Mueve el cursor táctil |
| RT | Toca la pantalla |
| Back + Start (1.5 s) | Volver al explorador |

## Log

Cada sesión escribe `xds360.log` junto a `default.xex` (la anterior queda en
`xds360-anterior.log`). Cada 5 segundos anota los FPS y el tiempo del núcleo
por cuadro. Si algo falla en la consola, mándalo.

## Cómo está armado

- `noods/`: núcleo NooDS (GPL-3.0) con el parche de Xbox de XenonDS, convertido
  para VS2010 por `tools/xdk/convert_noods.py`. **No se edita a mano:** se
  regenera con `scripts/fetch_noods.sh` y luego ese script.
- `compat/`: versiones propias de `<thread>`, `<mutex>`, `<atomic>`, `<chrono>`
  y `<condition_variable>` (VS2010 no las tiene), hechas con funciones del XDK.
- `frontend/`: `emu.cpp` habla con NooDS; `main.cpp` hace video, control, log y
  rutas. `font8x8_basic.h` es la fuente font8x8 de Daniel Hepper (dominio público).

## Pruebas en PC

- `tools/xdk/test_core_pc.sh`: compila el núcleo convertido con `compat/` y
  comprueba que las huellas de cuadros sean iguales a las de NooDS original.
- `tools/xdk/check_syntax.sh`: revisa todos los archivos como si fuera VS2010
  para Xbox, con encabezados falsos del XDK.
- `tools/xdk/sim/run_sim.sh`: corre el `main.cpp` real en la PC con un XDK
  simulado (dibuja en una imagen, botones de un guion) y saca capturas.
