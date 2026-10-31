# Rama experimental de rendimiento XDK

Base: `xdk-port`, commit `1484b7a`. Compilar **Release | Xbox 360** con
`xdk/xds360.sln`; hacer **Rebuild** al cambiar de rama. Este cambio entrega código
fuente. No incluye un `default.xex` compilado ni resultados de consola.

## Qué cambia

- **ARM:** 134 grupos del decodificador (2144 de sus 4096 entradas) pasan a llamadas
  directas a los mismos manejadores de NooDS. Los grupos con varios manejadores
  y las instrucciones desconocidas conservan la tabla. La comprobación de
  condiciones ARM ocurre antes del despacho, como antes. No se alteran ciclos,
  interrupciones, registros ni accesos a memoria del DS.
- **Pantallas:** el frontend mantiene las texturas entre refrescos si `getFrame`
  no entrega una imagen nueva. Los controles, el cursor y la presentación siguen
  actualizándose. Se renuevan las texturas al recibir imagen nueva, cambiar el
  formato o entrar al menú de pausa (que puede cambiar la ROM).
- **Salto de cuadros:** el menú ahora publica su valor mediante
  `Gpu::requestFrameSkip`. Antes cambiaba `Settings::frameskip`, pero la ruta Xenon
  usaba un valor atómico separado que no se actualizaba. Se aceptan los valores
  0–5 del menú; el cambio entra en un límite de cuadro completo. La captura a VRAM
  sigue ejecutándose cuando el juego la necesita, aunque no se muestre ese cuadro.
- **Afinidad:** los dos trabajadores 3D predeterminados usan contextos 2 y 4,
  repartidos entre núcleos físicos, en vez de los hermanos SMT 2 y 3. El emulador
  queda en 0, guardados en 1 y frontend en 5. El audio sigue usando 1. El beneficio
  depende de la carga; la consola debe confirmar esta distribución.
- **Log:** cada ventana de unos cinco segundos incluye cantidad de subidas de
  texturas, duración de ventana, salto y trabajadores 3D. `screen fps` cuenta
  refrescos del frontend, no imágenes nuevas ni velocidad del juego. Las subidas
  cuentan texturas, no cuadros: normalmente hay dos por imagen de DS.

## Por qué no basta con copiar un emulador de Android

El DS ejecuta dos CPU ARM con memoria compartida, DMA, temporizadores, vídeo 2D,
3D y sonido sincronizados por eventos. Aquí NooDS interpreta esas instrucciones
sobre PowerPC. La Xbox 360 tiene tres núcleos físicos con dos contextos de hardware
por núcleo; seis contextos no equivalen a seis núcleos completos. Dos trabajadores
pesados en el mismo núcleo compiten por recursos.

Un recompilador destinado a generar instrucciones ARM para un teléfono necesita
otro backend para generar PowerPC en Xbox 360. También necesita invalidación de
código, gestión de memoria ejecutable, coherencia de cachés de instrucciones y
datos, excepciones y comparación con el intérprete. Esta rama no implementa un
JIT ni incorpora código de DraStic. No promete 60 FPS ni compatibilidad universal.

El siguiente paso grande es medir el reparto CPU/2D/3D/audio en consola y después
abordar un recompilador ARM→PowerPC con pruebas diferenciales. Un render Xenos
requiere preservar profundidad, mezcla, orden de polígonos y captura de VRAM;
reemplazar el render por software sin esas reglas rompería juegos.

## Comparación en consola

1. Guarda tu `default.xex` anterior y una copia de `noods.ini` y las partidas.
2. Compila esta rama en Release. Copia el nuevo `default.xex` y conserva los
   mismos archivos de configuración. Usa la misma ROM, partida y escena de
   New Super Mario Bros. donde mediste unos 10 FPS.
3. Primero prueba salto 0, resolución nativa, sin filtro de ampliación y dos
   trabajadores 3D. Espera 30 segundos y conserva `xds360.log`.
4. Repite con salto 1 y luego 2. Cambia una opción a la vez. Menos cuadros
   mostrados no garantiza mayor velocidad de CPU: observa los FPS del juego y
   si la acción tarda menos tiempo real.
5. Para comparar solo el nuevo despacho ARM, añade `XDS_ARM_DIRECT=0` a las
   definiciones del preprocesador en **Release | Xbox 360** y haz Rebuild. El
   resto de mejoras permanece activo. Quita la definición para volver a activarlo.
6. Para comparar afinidad, vuelve temporalmente a `{0,1,2,3,4}` en `main.cpp`,
   manteniendo las demás opciones. No se cambió la rama `main` ni `xdk-port`.

Si falla la compilación, se necesita el error completo de VS2010. Si falla o se
congela en consola, se necesita el log y la escena exacta; las pruebas en PC no
pueden validar PowerPC, XAudio2, Direct3D real ni BadUpdate.

## Regeneración y pruebas

`convert_noods.py` llama a `optimize_noods.py` después de convertir los archivos
para VS2010. El include `arm_direct.inc` se deriva de la tabla de NooDS y se
versiona para que Windows no necesite Python al compilar. Las sustituciones
fallan si cambia el código base, en vez de omitir silenciosamente un arreglo.

```sh
python3 tools/xdk/test_optimizations.py
bash tools/xdk/test_core_pc.sh
bash tools/xdk/check_syntax.sh
bash tools/xdk/sim/run_sim.sh
```

La primera prueba compila el include en C++03 y comprueba las 4096 rutas con el
despacho activado y desactivado. Las pruebas del núcleo verifican imágenes,
capturas y cadencia. Los benchmarks sintéticos de PC no predicen los FPS de un
juego comercial en Xbox 360.

Créditos: NooDS de Hydr8gon (GPL-3.0 o posterior), port y optimizaciones previas
XenonDS de jmay46, y port XDK xDS360. El decodificador conserva los manejadores y
la tabla originales de NooDS; no incorpora un núcleo de terceros adicional.

## Fuentes técnicas

- [NooDS fijado por el proyecto](https://github.com/Hydr8gon/NooDS/tree/4cb1439c330248b26cdf5f8dbcfa4e5ac53dcda0):
  intérprete ARM/THUMB, memoria y planificador; se conserva su implementación.
- [Microsoft: programación multinúcleo en Xbox 360](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/coding-for-multiple-cores):
  tres núcleos con dos hilos de hardware cada uno y necesidad de medir la afinidad.
- [Microsoft: numeración de hilos de hardware](https://learn.microsoft.com/en-us/previous-versions/windows/silverlight/dotnet-windows-silverlight/bb355239(v=vs.95)):
  parejas 0/1, 2/3 y 4/5.

El despacho directo puede aumentar el tamaño del código y la presión de caché.
Por eso se conserva la opción de desactivarlo para comparar en la consola.

## Validación de esta entrega

- 4096 rutas ARM comprobadas con el despacho activado y desactivado.
- Núcleo compilado en PC con las capas de compatibilidad XDK: imagen
  `4550DDC5`, 60 casos de captura con saltos 1–5, 96 casos de bancos VRAM y
  2000 cuadros de cambios de cadencia; todos pasan.
- Regeneración de los archivos modificados desde la base: salida idéntica.
- Frontend real ejecutado con XDK simulado: arranque, pantallas, pausa, ajustes,
  idioma y regreso al explorador. Salto 5 probado en otra ejecución.
- Sintaxis de los cuatro `.cpp` modificados comprobada con GCC y encabezados XDK
  falsos (`_XBOX`, `_MSC_VER=1600`, big-endian). No sustituye la comprobación con
  el compilador de Microsoft; el comprobador Clang del proyecto no se ejecutó.

No se compiló con VS2010/XDK ni se probó una Xbox 360 real. El benchmark de la
ROM sintética sigue dominado por un bucle de espera y sus variaciones de tiempo
no demuestran una mejora en New Super Mario Bros. La medida útil pendiente es
la misma escena del juego en la consola.

## Siguiente cambio: vídeo RGB6 nativo

La versión `0.2.2-native-video-dev` añade una ruta de adquisición directa del
cuadro original y conversión de color en shaders Xenos. Ver
[VIDEO_NATIVO_XDK.md](VIDEO_NATIVO_XDK.md) para su alcance, pruebas y respaldo.
