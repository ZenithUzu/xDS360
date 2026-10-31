# xDS360: primer port xSDK

Rama `experiment/xsdk-port`, basada en `xdk-port` (`1484b7a`). Usa el NooDS ya
versionado en `xdk/noods/core`, sin enlazar bibliotecas del XDK ni añadir
`xdk/compat` a la ruta de inclusión. Los hilos, mutexes, atomics y relojes son
los de libstdc++/xSDK. Se reutilizan únicamente los ayudantes de byte order y
los arrays de objetos del código convertido.

## Compilar en Debian/WSL

Requiere xSDK construido en `~/xsdk`, su toolchain en `~/xsdk-gcc` y glslang-xenon
instalado en `~/xsdk-gcc/bin/glslang`, como en la guía de xSDK. Desde el repositorio:

```bash
cmake -S platform/xsdk -B build-xsdk -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/xsdk/cmake/xsdk.cmake" \
  -DXSDK_TOOLCHAIN="$HOME/xsdk-gcc" -DCMAKE_BUILD_TYPE=Release
cmake --build build-xsdk --parallel 4
```

Salida: `build-xsdk/xds360-xsdk.xex`. Los shaders se compilan y se incluyen en
el ejecutable; no hay que copiar archivos de shaders a la consola.

## Primera prueba en consola

Crea una carpeta aparte, para no reemplazar la versión XDK que ya funciona:

```text
Hdd1:\xDS360-xsdk\xds360-xsdk.xex
Hdd1:\xDS360-xsdk\game.nds
```

`game.nds` debe ser una copia de tu ROM de NSMB. Para continuar tu partida,
copia también su `.sav` como `game.sav` en esa carpeta. No muevas el original.

- Arranque directo sin exigir BIOS/firmware. No hay explorador todavía.
- Pantalla superior a la izquierda y táctil a la derecha, a resolución nativa
  escalada por Xenos. El núcleo todavía dibuja 2D/3D por software.
- A/B/X/Y, cruceta, LB/RB, Start y Back corresponden a las etiquetas del DS.
- Stick derecho mueve el cursor; RT toca la pantalla.
- **LB + RB + Start + Back** sale y guarda. Sin ROM o después de un error, Back
  también sale. Se intenta guardar el cartucho cada cinco segundos.
- Sin ROM, aparecen dos colores y el aviso de que falta `game.nds`.
- **No tiene audio todavía.** Tampoco pausa, explorador o estados de guardado.

El HUD muestra los cuadros emulados y presentados por segundo. El programa
intenta escribir `xds360-report.txt` junto al XEX, independientemente del log
automático de xSDK. `Reporte: OK` indica que pudo abrirlo; `NO DISPONIBLE`, que
no pudo. El SDK también recibe los mensajes por `xsdk_log_write` y puede usar
su `net_log.txt` habitual para enviarlos por red.

## Qué se está comparando

La primera meta es que arranque el núcleo y NSMB con el toolchain nuevo. **No se
promete una subida de FPS.** Esta versión no incluye los cambios de la rama
`experiment/powerpc-core-optimization`; conserva la base que se eligió para
el port. El audio está desactivado y el intérprete sigue siendo NooDS.

El hilo principal solicita el contexto 0. Los dos trabajadores 3D solicitan
2 y 4 desde su entrada; se generan esas llamadas en el directorio de build
para no modificar las fuentes compartidas con el port XDK. Los trabajadores
nativos se crean como pide NooDS, sin el pool específico de la rama XDK: esa
diferencia debe medirse antes de comparar rendimientos.

La cola de texturas es doble. Cada textura espera su fence antes de recibir
el siguiente cuadro; el núcleo no escribe memoria que la GPU aún esté leyendo.
La presentación y el acceso a GPU siguen las APIs de los ejemplos y tests de
xSDK (`samples/triangle`, `tests/test_texture.c`). La conversión de RGB6 a RGB8
sigue en CPU para mantener una primera referencia sencilla.

## Verificación realizada y límites

En PC, el mismo núcleo compiló usando los headers estándar de thread/atomic,
y pasaron las cinco pruebas CTest de arranque, framepath, captura, bancos VRAM
y 2000 cuadros de cadencia. El frontend pasó sintaxis C++17 con los encabezados
reales de xSDK y un encabezado de shaders simulado. Esto **no** valida el código
PowerPC ni compila los shaders: el primer build cruzado y la ejecución en
Xbox se deben comprobar con tu toolchain y consola.

Para repetir las pruebas portables sin xSDK:

```bash
cmake -S platform/xsdk -B build-xsdk-host -DXDS_HOST_TESTS=ON
cmake --build build-xsdk-host --parallel 4
ctest --test-dir build-xsdk-host --output-on-failure
```

Si la compilación falla, conserva el primer error y las últimas líneas. Si
el XEX arranca, reporta pantalla, controles, FPS de menú/gameplay y el contenido
de `xds360-report.txt` si se creó. No se ha ejecutado esta versión en hardware.

## Créditos y licencias

NooDS de Hydr8gon y el ejecutable derivado: GPL-3.0 o posterior; port original
XenonDS de jmay46 y port XDK xDS360. xSDK de xenosworks es una dependencia externa
(MIT y avisos BSD de sus componentes; ver su LICENSE y THIRD_PARTY_NOTICES.md).
No se incluye el SDK de Microsoft ni una ROM comercial.
