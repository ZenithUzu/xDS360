# Vídeo nativo RGB6 → Xenos (experimental)

Versión de identificación: `0.2.2-native-video-dev`. Se añade a la rama
`experiment/powerpc-core-optimization`; no sustituye `main` ni `xdk-port`.

## Trabajo que se elimina de la CPU

Antes, para cada imagen nueva de DS:

1. NooDS producía sus dos pantallas en RGB6 empaquetado (18 bits de color, más
   indicadores internos, dentro de palabras de 32 bits).
2. `Gpu::getFrame()` convertía 98 304 píxeles a RGB8 y escribía el búfer de la UI.
3. El frontend copiaba ese búfer a las texturas Direct3D.
4. Xenos escalaba y mostraba las texturas.

Ahora, cuando el modo es compatible:

1. La UI adquiere temporalmente el cuadro RGB6 original de la cola de NooDS.
2. El frontend copia directamente sus dos mitades a las texturas Direct3D.
3. Devuelve el cuadro al núcleo después de terminar la copia.
4. **Xenos decodifica RGB6, expande cada canal y escala la imagen en un shader.**

Se elimina la conversión RGB6→RGB8 por CPU y la escritura/lectura del búfer
intermedio. Son **98 304 conversiones de píxel por imagen** y **768 KiB de tráfico
de memoria intermedio** que se evitan (384 KiB de escritura y 384 KiB de lectura).
A 60 imágenes nuevas por segundo serían 45 MiB/s. Esto es trabajo eliminado por
el diseño, no una medida de rendimiento real en Xbox.

Todavía existe la copia desde el núcleo a las texturas. No se llama a esto
«cero copias». El render 2D/3D del DS sigue siendo el de NooDS por software:
el shader nuevo hace la conversión y presentación final, no rasteriza los
polígonos del juego. Las CPU ARM siguen interpretándose.

## Color y filtro

`xdk/frontend/xds_native_video.h` contiene dos shaders:

- Punto (`ps_2_0`): una muestra RGB6, decodificación y expansión a RGB8.
- Lineal (`ps_3_0`): cuatro muestras puntuales, decodificación de cada una y
  después interpolación. Nunca interpola los bytes empaquetados, porque los
  acarreos entre campos producirían colores incorrectos.

La expansión reproduce `canal * 255 / 63` con truncamiento, igual que NooDS.
Los indicadores internos no forman parte del color; la opacidad final es 255.
La textura conserva el formato lineal A8R8G8B8 y la misma ruta de memoria que ya
usaba el frontend: la nueva interpretación ocurre en el shader.

## Protección y respaldo

- Un solo consumidor puede adquirir el cuadro. Mientras está adquirido, las
  otras funciones de extracción lo rechazan. La devolución doble es inocua.
- El productor conserva el cuadro mientras se copian las texturas. El shader
  no conserva un puntero al cuadro del núcleo: lee su propia textura.
- La consulta del límite de dos cuadros en cola también usa el mutex; ya no
  consulta el tamaño de la cola mientras el consumidor lo modifica.
- GBA, ghosting, resolución alta y filtro «Upscaled» conservan la ruta RGB8.
- Si compilar o crear el shader correspondiente falla, se usa automáticamente
  la ruta RGB8 para ese filtro. Los errores aparecen en `xds360.log`.
- La opción `gpuNativeVideo=0` en `noods.ini` desactiva toda la ruta nueva. El
  valor predeterminado es 1. Cambiarla con el emulador cerrado.

## Cómo probar en Xbox

1. Sincroniza la rama experimental en GitHub Desktop. Abre `xdk/xds360.sln` y
   haz **Rebuild, Release | Xbox 360**. Copia `default.xex` a la consola.
2. Prueba la misma escena/partida de New Super Mario Bros. con salto 0 y las
   demás opciones iguales. El filtro «Nearest» usa el shader de punto;
   «Linear» usa el shader lineal. Prueba cada uno por separado.
3. El log debe identificar `0.2.2-native-video-dev`, informar el estado de ambos
   shaders y mostrar subidas `native=` mayores que cero mientras juegas.
   `native=0` significa que esa ventana no utilizó el camino nuevo.
4. Guarda el log. Cierra, cambia solo `gpuNativeVideo=0` y repite la misma escena.
   Compara los FPS del juego, no solo los refrescos de pantalla.
5. Para informar un fallo: log completo, filtro, escena y si ocurre también con
   `gpuNativeVideo=0`. Que el shader compile no garantiza que sea más rápido.

## Pruebas

`bash tools/xdk/test_core_pc.sh` también compila y ejecuta
`tests/xdk_native_video_test.cpp`:

- 262 144 colores RGB6 con dos patrones de indicadores: 524 288 comparaciones
  de la fórmula de decodificación con la expansión original.
- 65 536 posiciones de interpolación, con casos que cruzan campos de bits.
- 32 cuadros comparados píxel a píxel con `Gpu::getFrame()`, alternando pantallas
  y modos de filtro.
- Adquisición duplicada, devolución doble, producción de cuadros mientras se
  mantiene uno adquirido y rechazo de modos incompatibles.
- Un microbenchmark de **CPU solamente** compara conversión+subida simulada con
  adquisición+subida simulada. No incluye el costo del shader ni predice FPS.

El simulador reconoce ambos shaders y permite simular su fallo mediante
`SIM_FAIL_NATIVE_SHADERS=1`. No compila HLSL ni emula el hardware Xenos: comprueba
la integración, los datos y el respaldo. La compilación de HLSL con D3DX/XDK y
el comportamiento real de PowerPC, las texturas y la GPU necesitan la consola.

La regeneración está en `tools/xdk/native_video.py`, llamado desde el pase de
optimización del conversor. Los proyectos de VS2010 incluyen el nuevo encabezado.

## Referencias y alcance

Se mantiene el núcleo NooDS de Hydr8gon (GPL-3.0 o posterior), el port XenonDS y
el frontend XDK de xDS360. Esta ruta es una implementación nueva; no contiene
código de DraStic ni afirma portar su recompilador.

- [Microsoft: tex2D](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-tex2d).
- [Microsoft: modelos de shader y perfiles de compilación](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/specifying-compiler-targets).
- [melonDS: presentación OpenGL y render 3D son funciones diferentes](https://melonds.kuribo64.net/comments.php?id=90&p=1).

El beneficio depende del cuello de botella. Si la mayor parte del tiempo se
consume interpretando ARM, este cambio de presentación no lo elimina. Un
recompilador ARM→PowerPC y un render 3D Xenos serían proyectos adicionales.

### Resultado de las comprobaciones en PC

Pasaron la suite previa del núcleo (incluidos 60 casos de captura, 96 casos de
VRAM y 2000 cuadros de cadencia), el test nativo y la regeneración reproducible.
El simulador produjo imágenes idénticas con un patrón de píxeles rojos/azules
para filtros punto y lineal, en tres configuraciones: ruta nativa, desactivada
por INI y shaders que fallan. Para repetir esa comparación, después de construir
el simulador:

```sh
python3 tools/xdk/test_native_video_sim.py
```

En una ejecución del microbenchmark de PC, 240 extracciones y copias acumularon
89 732 µs con RGB8 por CPU y 7834 µs con la adquisición RGB6. La medida excluye
la conversión que ahora hace la GPU; no mide el rendimiento total de un juego.
No se construyó un `.xex` con el XDK ni se ejecutó esta versión en Xbox 360 real.
