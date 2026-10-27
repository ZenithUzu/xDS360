# Instrucciones para Claude

## El proyecto

**xDS360**: emulador de Nintendo DS para Xbox 360 homebrew. Es un fork de XenonDS
(github.com/jmay46/XenonDS), que usa el núcleo NooDS con LibXenon. Nuestra versión se
compila con **VS2010 y el XDK** y genera un `.xex` que se abre desde Aurora.
- Repositorio: github.com/ZenithUzu/xDS360
- El núcleo es NooDS fijado en `upstream.lock`, con el parche `patches/noods-xenon.patch`
  (`scripts/fetch_noods.sh` lo descarga en `third_party/noods`, que no se sube).
- La interfaz sale de github.com/ZenithUzu/Apariencia-Emulador (leer su README, sección
  "Adaptarlo a otro emulador"). Proyecto anterior de referencia: github.com/ZenithUzu/AdvanXe360.

## Cómo trabajar conmigo

- **Responde siempre en español**, también en las notas mientras trabajas. Los mensajes de commit van en inglés.
- **Estoy aprendiendo:** explica paso a paso y con palabras simples, sin dar por hecho que conozco los términos.
- **Usa /ponytail:** el cambio más pequeño que resuelva el problema completo. Termina cada respuesta con una o dos líneas sobre lo que no hiciste o no comprobaste, y los riesgos.
- **No te apresures:** si algo no está claro o se puede hacer de varias formas que cambian el resultado, pregunta antes. Si una decisión es mía (qué publicar, qué borrar, qué va a `main`), espera mi respuesta.
- **Cuando algo falle en la consola, pide el log** (o agrega una línea de log con tiempos) antes de adivinar la causa.

## Ramas y publicación

- Trabaja **solo en ramas de prueba**. **No toques `main`** hasta que yo confirme que todo funciona en la consola.
- Si un cambio debe ir a varias ramas (por ejemplo, una versión con y otra sin una función), aplícalo en todas y dime cuáles actualizaste.
- Las versiones oficiales salen de `main`; las experimentales, como *pre-release*.
- Identidad para los commits: nombre **Axel** y el correo de mi cuenta de GitHub (no se escribe aquí porque el repositorio es público).

## Cómo compilo y pruebo

- **Visual Studio 2010 + Xbox 360 XDK**, configuración **Release | Xbox 360**.
  - **Build** normalmente. **Rebuild** al cambiar de rama, con errores raros de enlace (`LNK...`) o si un arreglo "no se aplicó".
- Sincronizo con **GitHub Desktop** (Pull) y copio `default.xex` a la consola por **FTP**.
- Consola con **BadUpdate + Aurora**.
- Te paso los errores de compilación y el log del emulador como archivos adjuntos.

## Lo aprendido en AdvanXe360 (Xbox 360 + VS2010)

**Compilador (VS2010, C++03 / C89):**
- Sin `snprintf` (solo `_snprintf`, que no termina el texto), sin inicializadores de C99 (`.campo = valor`), sin variables declaradas en medio de un bloque de C.
- `__restrict` cambia el nombre de una variable al enlazar: declárala exactamente igual que en su `.h`.
- Revisa la sintaxis en la PC con encabezados falsos del XDK antes de que yo compile.

**La consola:**
- **El procesador es mucho más lento que una PC** en código que recorre memoria byte por byte (en AdvanXe360, unas 40 veces). Mide en la PC, pero asume que en la consola será mucho más lento.
- Nada pesado en el hilo del juego o del menú: decodificar imágenes, redimensionar, comprimir, crear o liberar texturas (`BlockUntilIdle`), leer archivos. Hazlo antes (al cargar), en otro hilo o solo cuando el usuario se detiene.
- Hilos: `XSetThreadProcessor`. En AdvanXe360: juego en 0, audio en 2, trabajos en segundo plano en 4 y 5.
- En Xbox 360 las funciones `Interlocked` **no** son barreras de memoria: usa `MemoryBarrier()` al pasar datos entre hilos.
- Es **big-endian** (los `.wav` y muchos formatos de PC vienen al revés).
- Nombres de archivo de máximo **42 caracteres** (FATX). Los que se copian por FTP llegan en **UTF-8**: la ñ y los acentos ocupan 2 bytes.
- Las rutas son del estilo `game:\carpeta\archivo`.

**Calidad:**
- Lógica nueva y no trivial (un analizador, un búfer, cálculos): una prueba pequeña en la PC antes de subirla.
- Interfaz: revísala con un simulador en PC y mándame capturas cuando cambie algo visible.
- Respeta las licencias: da créditos a cada librería, imagen o idea ajena (en el README y en *Acerca de*), y no incluyas material con licencia "no comercial" o sin permiso.
