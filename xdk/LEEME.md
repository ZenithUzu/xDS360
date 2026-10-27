# xDS360 con VS2010 y el XDK

Versión en desarrollo: arranca **una** ROM, muestra las dos pantallas y los FPS.
Todavía no tiene sonido, menú ni explorador de ROMs.

## Compilar

1. Abre `xdk/xds360.sln` con Visual Studio 2010 (con el XDK de Xbox 360 instalado).
2. Elige **Release | Xbox 360** y dale a **Build**.
3. El ejecutable sale en `xdk/Release/default.xex`.

## Poner en la consola

```
(carpeta del emulador)\
    default.xex
    roms\
        juego.nds        <- una sola ROM; si hay varias, se usa la primera por orden alfabético
```

- El nombre de la ROM debe tener **máximo 42 caracteres** (límite de FATX).
- La partida se guarda junto a la ROM (`juego.sav`) cada 5 segundos y al salir.
- Opcional: `bios7.bin`, `bios9.bin` y `firmware.bin` junto a `default.xex`. Sin ellos usa arranque directo.

## Controles

| Xbox | DS |
|---|---|
| A, B, X, Y | A, B, X, Y |
| LB, RB | L, R |
| Start, Back | Start, Select |
| Cruceta | Cruceta |
| Stick derecho | Mueve el cursor táctil |
| RT | Toca la pantalla |
| Back + Start (1.5 s) | Salir al dashboard |

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
