# xDS360 con VS2010 y el XDK

Versión en desarrollo con la interfaz de consola de NooDS (la misma que usa
su versión de Wii U): explorador de archivos, menú de pausa, guardado rápido
(estados), trucos y ajustes.

## Compilar

1. Abre `xdk/xds360.sln` con Visual Studio 2010 (con el XDK de Xbox 360 instalado).
2. Elige **Release | Xbox 360** y dale a **Rebuild** (la lista de archivos cambió).
3. El ejecutable sale en `xdk/Release/default.xex`.

## Poner en la consola

Copia solo `default.xex` a una carpeta de la consola. Al abrirlo crea solo:

```
(carpeta del emulador)\
    default.xex
    roms\       <- pon aquí tus .nds (también puedes usar subcarpetas)
    saves\      <- partidas guardadas (juego.sav)
    states\     <- guardado rápido (Save State / Load State)
    cheats\     <- trucos
    noods.ini   <- ajustes (se crea solo)
    xds360.log
    bios7.bin, bios9.bin, firmware.bin   <- opcionales; sin ellos usa arranque directo
```

- Los nombres de archivo deben tener **máximo 42 caracteres** (límite de FATX).
- Si un `noods.ini` viejo da problemas, bórralo y se vuelve a crear.

## Explorador

Abre en `roms\`. Con **B** subes de carpeta; desde la carpeta del emulador
llegas a las unidades: disco duro (`hdd`) y USB (`usb0` a `usb2`).

| Botón | Qué hace |
|---|---|
| Cruceta o stick izquierdo | Moverse |
| A | Abrir carpeta o jugar |
| B | Subir una carpeta |
| X | Ajustes |
| Start | Ir a `roms\` |
| Back | Salir al dashboard |

## Controles en el juego

| Xbox | DS |
|---|---|
| A, B, X, Y | A, B, X, Y |
| LB, RB | L, R |
| Start, Back | Start, Select |
| Cruceta o stick izquierdo | Cruceta |
| Stick derecho | Mueve el cursor táctil |
| RT | Toca la pantalla |
| Clic del stick derecho (RS) | Menú de pausa |
| LT (mantener) | Avance rápido |
| Clic del stick izquierdo (LS) | Cambiar las pantallas de lugar |

Los botones se pueden cambiar en **Ajustes > Controls** (botón X dentro de Ajustes).

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
- `noods/ui/`: la interfaz de consola de NooDS (menús, explorador, imágenes),
  también convertida por ese script. Al arrancar, `main.cpp` cambia los
  símbolos de botones de su fuente por los íconos de Xbox.
- `frontend/xbox_buttons.cpp`: los íconos de los botones (A, B, X, Start y
  Back), imágenes del paquete "Xbox 360 Controller Button Images" de Microsoft
  (XNA Game Studio), licencia Ms-PL (`licencias/XNA-botones-MsPL.txt`). Lo
  genera `tools/xdk/gen_xbox_buttons.py` desde la carpeta `ButtonImages` del
  paquete; no se edita a mano.
- `frontend/main.cpp`: la capa de Xbox que pide esa interfaz: video (Direct3D),
  control, sonido (XAudio2), unidades, log y rutas.
- `compat/xds_files.cpp`: traduce las rutas de NooDS (`/game:/roms`) a las de
  Xbox (`game:\roms`) y lista carpetas y unidades.
- `tools/xdk/update_vcxproj.py`: vuelve a escribir la lista de archivos del
  proyecto de VS2010 cuando cambia el núcleo.

## Pruebas en PC

- `tools/xdk/test_core_pc.sh`: compila el núcleo convertido con `compat/` y
  comprueba que las huellas de cuadros sean iguales a las de NooDS original.
- `tools/xdk/check_syntax.sh`: revisa todos los archivos como si fuera VS2010
  para Xbox, con encabezados falsos del XDK.
- `tools/xdk/sim/run_sim.sh`: corre el `main.cpp` real en la PC con un XDK
  simulado (dibuja en una imagen, botones de un guion) y saca capturas.
