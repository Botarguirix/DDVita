# Dungeon Deathball Vita: core

Lógica del juego portada a C++17, sin dependencias de render. Se compila igual para PC (pruebas) y para PS Vita.

## Contenido (hito 1)

| Archivo | Equivale a (C# original) |
|---|---|
| `src/core/mono_random.*` | `System.Random` del mscorlib de Mono, más `SessionSeed.SeededRandom` |
| `src/core/mono_string.*` | `String.GetHashCode` de Mono (seed del reto diario) |
| `src/core/calendar.*` | `SeasonalEvents`, `Helper.GetDateStandardised`, `SessionSeed.GetDailyChallengeSeed` |
| `src/core/gamedata.*` | ScriptableObjects y singletons de escena leídos de `data/gamedata.json` |
| `src/core/session_generator.*` | `SessionGenerator` más las partes con RNG de `PerkManager`, `PlayerClasses`, `HumanPlayerGenerator` y `PlayerSpriteGenerator` |
| `tools/sessiondump.cpp` | Volcado canónico de una partida (para comparar) |

Se replican a propósito estos bugs del original, porque cambian el resultado:
- `GetRandomNameOrBio` elimina el valor y no la posición, así que los nombres pueden repetirse.
- `GetSpecialEnemyTypesForStage` usa `<=` y elige un tipo de enemigo especial de más.
- `SeededRandom` con `min > max` consume igualmente un número del RNG.
- El rango de Navidad (356→5) nunca se cumple, así que el evento no se activa.
- `BuildVersion.IsOlderThan` no es una comparación lexicográfica.

## Simulación de una stage (hito 2)

`src/sim` traduce clase por clase el gameplay: GameController, EnemyTurn, FieldManager, Actor/Character/Enemy/Player, Attack*, Ball, EnemyActionPlanner, IntentionMarker, HazardManager, UndoManager, StageScore y Gold. Las corrutinas se ejecutan de forma síncrona, en el orden en que Unity las reanuda. Cuando el original las solapa (los golpes de un mismo ataque, el rebote del balón), el cambio de estado se parte por el `yield` (`Actor::HitBegin` / `HitEnd`). Cada paso visible se emite como `Event` para la capa de presentación.

| Archivo | Contenido |
|---|---|
| `sim/sim.h` | Tipos y API (`Run` = sesión, `Game` = una stage) |
| `sim/run.cpp` | HumanPlayer (stats), HumanTeam, BallMods, LeagueManager, Score |
| `sim/field.cpp` | FieldManager |
| `sim/actors.cpp` | Actores, balón, tesoros, peligros |
| `sim/attack.cpp` | Ataques |
| `sim/ai.cpp` | Intenciones y planificador enemigo |
| `sim/game.cpp` | Fases, turnos, managers, comandos, undo, puntuación |
| `tools/ddplay.cpp` | Jugar en consola: `ddplay data\gamedata.json <seed> [modo] [equipo]` o `ddplay data\gamedata.json daily` |
| `tools/simtest.cpp` | Partidas aleatorias con invariantes y comprobación de determinismo |

Comportamientos del original que se replican a propósito:
- Un enemigo matado sin atacante (Halloween, `DamageAllEnemies`) provoca un NRE en `CheckKillerForPerk`. Por eso se queda en la lista de enemigos.
- `IntentionMarkerEnemy.Show` marca las casillas objetivo aunque la intención se cancele.
- `GA_Player_Take` no llama a `BallAcquired`.
- `AttackCharge` no llama a `OnMoveToPositionStarted`.

```powershell
& $env:TEMP\ddvita-pc\simtest.exe data\gamedata.json 1 200 [modo] [verbose] [trialInicial] [fecha] [rest]
```

## Render y juego en ventana (hito 3)

`src/render` dibuja una stage con OpenGL de función fija y arrays de cliente, lo mismo que soporta vitaGL. La escena se renderiza a 640×360 y se escala con point sampling, igual que el Flashback94 de la cámara del juego.

| Archivo | Contenido |
|---|---|
| `render/gfx.*` | Renderer: texturas, modos opaco/cutout/alpha, frame a baja resolución, 2D |
| `render/assets.*` | Carga `data/render`: sprites, arenas, swaps por trial, capas del jugador (SpriteMaker), animaciones, fuente |
| `render/stage_view.*` | Cámara, arena, casillas, actores, intenciones, efectos y reproducción de los `Event` con sus tiempos |
| `app/game_app.*` | Bucle de juego común a PC y Vita: HUD provisional, paso de stage a stage, guardado |
| `app/main.cpp` | Plataforma PC: ventana SDL2, entrada (teclado o mando) y audio |

```powershell
# exportar el render desde la copia del usuario (necesita extracted/ del hito 1)
python -E -P tools\export_render.py "<ruta del juego>" extracted data\render
copy port\vita\font\font.* data\render\     # la fuente del HUD es del port
& $env:TEMP\ddvita-pc\ddgame.exe data [seed|daily] [modo] [equipo]
```

Controles: con las flechas mueves el cursor y con Enter/Espacio haces la acción de esa casilla. Q pasa o coge el balón, E chuta, R descansa, U deshace, Tab cambia de jugador, I muestra la información de la casilla, Y/N responde a la sugerencia de undo y F1 alterna log → log + rendimiento → nada. Con mando: cruceta, A, X, Y, B, Back y L/R.

Para pruebas: `--auto` juega solo, `--speed k` acelera el tiempo y `--shot out.bmp segundos` guarda una captura y sale (sin tocar el guardado).

`data/render` sale de los assets del juego: no se distribuye. `font.tex` es una fuente provisional de Pillow y no es dato del juego.

## Vita, audio y guardado (hito 4)

| Archivo | Contenido |
|---|---|
| `app/platform.h` | Interfaz de plataforma: init, entrada, swap, reloj, hilo de audio, rutas, nombres de botones |
| `app/platform_vita.cpp` | Vita: vitaGL, SceCtrl (stick izquierdo = cruceta), SceAudioOut en un hilo, relojes 444/222/222/166 |
| `audio/mixer.*` | Mezclador por software a 48 kHz: SFX en RAM, música y jingles en streaming, grupos con los volúmenes por defecto del AudioMixer original |
| `audio/sound.*` | SoundCues, Jukebox (playlist, boss, out-of-time, triumphant), jingles, voces de jugador y sonido de cada evento |
| `app/save.*` | Guardado de la partida en curso como registro de entradas |

```powershell
# exportar el audio desde la copia del usuario (necesita extracted/ del hito 1)
python -E -P tools\export_audio.py "<ruta del juego>" extracted data\audio
```

`export_audio.py` convierte a IMA ADPCM (`.dda`) solo los clips que usa el juego: 465 clips y 102 cues, unos 76 MB. `data/audio` sale de los assets del juego: **no se distribuye**.

**Guardado.** `run.json` guarda la seed, el modo, el equipo, la fecha y, por cada stage superada, el balón elegido, los índices de comando y las compras entre stages (mejora = `jugador*10 + stat`, recluta = `100 + índice`). Las respuestas al undo van como −1 (sí) y −2 (no). Al continuar, la simulación determinista repite esas stages sin pantalla y la partida sigue al principio de la stage en curso. Como en el original, el guardado solo se escribe con Save & Quit y se borra al continuar: se carga una vez. Si el registro ya no se puede repetir (por ejemplo, con otro `gamedata.json`), esa partida empieza de nuevo. Los guardados v1 (sin balón ni compras) se siguen cargando.

**Vita.**
- Los datos van en `ux0:data/ddvita/` (`gamedata.json`, `render/`, `audio/`, `loc/`, `help.json`, `achievements.json`); el guardado, `settings.json` y `meta.json` también.
- vitaGL compila sus shaders al arrancar, así que hace falta `ur0:data/libshacccg.suprx`, como en cualquier homebrew con vitaGL.
- Controles: cruceta o stick para el cursor, Cruz para la acción de la casilla (y "sí"), Círculo para descansar (y "no"), Cuadrado para pasar o coger, Triángulo para chutar, Select para deshacer, L para cambiar de jugador, R para la información de la casilla, Start para la pausa y Select+Start para log/rendimiento.
- El VPK solo lleva `eboot.bin` y la LiveArea, que generan imágenes originales con `tools/make_livearea.py`: ningún asset del juego.

## Menús e interfaz (hito 5a)

| Archivo | Contenido |
|---|---|
| `app/app.h`, `app/game_app.cpp` | Flujo entre pantallas como GameFlow/StageEndUI: título → modo → equipo → inicio de stage → juego (pausa, opciones, derrota) → resultados → gestión del equipo → "Keep Going?" → … → final |
| `app/screens.cpp` | Dibujo de cada pantalla |
| `app/settings.*` | Volúmenes y sugerencias de undo (`settings.json`, junto al guardado) |
| `ui/ui.*` | Espacio virtual de 480×272 escalado a entero, 9-slice, botones tintados, texto con `<color>` y ajuste de línea |
| `ui/text.*` | Textos localizados (`data/loc/<idioma>.json`) y formato Smart Strings (`{name}`, `:plural:`, `:cond:`) |

```powershell
# textos y fuentes pixel del juego (necesita extracted/ del hito 1)
python -E -P tools\export_loc.py extracted data\loc
python -E -P tools\extract_fonts.py "<ruta del juego>" extracted\fonts
python -E -P tools\make_font.py data\render extracted\fonts\EffortsPro.ttf 16 font_s
python -E -P tools\make_font.py data\render extracted\fonts\SinsGold.ttf 16 font_m
python -E -P tools\make_font.py data\render extracted\fonts\CompassGold.ttf 16 font_l
```

Sin `data/loc` se muestran los textos en inglés del código; sin `font_s/m/l` se usa la fuente provisional. Ambos salen del juego: **no se distribuyen**.

**How to Play e información (hito 5b).** `tools/export_help.py extracted data\help.json` exporta las 15 páginas de How to Play (textos, subtítulos e imágenes), las descripciones de casilla (`TileInfoConfig`) y los textos de las ventanas de información. La página de controles no es la del original: el port la dibuja con sus propios botones. How to Play se abre desde el título y desde la pausa (arriba/abajo cambia de página; izquierda/derecha desplaza una página larga).

En juego, R en Vita (I en PC, RB con mando) activa la ventana de información de la casilla bajo el cursor: enemigo (vida, orden de actuación, plan, movimiento, ataque), jugador (clase, perk, stamina, AP y costes), balón, gema, trampas, muros, casillas peligrosas y línea de meta. Se coloca en el lado contrario al cursor. `app/help.*` carga los datos y `app/screens_help.cpp` dibuja ambas cosas. Sin `help.json`, How to Play solo muestra la página de controles.

## Logros y estadísticas (hito 6a)

`tools/export_achievements.py extracted data\achievements.json` exporta los 84 logros en el orden del original: nombre, descripción, si están ocultos, el objetivo de los de progreso y los iconos (normal y bloqueado). `prepare_data.py` lo ejecuta si falta. Es dato del juego: **no se distribuye**.

| Archivo | Contenido |
|---|---|
| `sim/meta.*` | `Achievement`, `Stat` y la interfaz `MetaSink`. Comprobaciones de logros de AchievementManagerDeathball (golpes, muertes, pases, fin de stage y de partida) |
| `app/meta.*` | `Meta`: logros desbloqueados, estadísticas, progreso por equipo, modos completados y récords. Se guarda en `meta.json`, junto al guardado |
| `app/screens_meta.cpp` | Pantalla Stats & Achievements (cuadrícula de 12 columnas, ficha del logro, pestaña de estadísticas con L en Vita o Tab en PC) y el aviso "Achievement Unlocked!" |

**Desbloqueos (hito 6b).** Como en el original:
- Al ganar la última stage de un trial, el equipo da sus llaves de calavera hasta ese trial: `keysAwardedPerTrial`, que vale 2 en Hodgepodge y 1 en los demás. Nunca las da dos veces. El aviso "Skull Keys Acquired!" sale antes de "Keep Going?".
- Para desbloquear un equipo hacen falta `unlockCost` llaves y, si lo pide, su logro (`requiredAchievement`). Hasta tener la primera llave o un equipo desbloqueado, la selección de equipo se salta y se juega con Hodgepodge.
- Los modos piden un número de logros (`unlockCriteriaAchievementCount`) y logros concretos. Al volver al título se avisa de cada modo nuevo y de los packs de música B-D, que entran en el shuffle.

Todo sale de `gamedata.json`. `ddgame data --meta <archivo> --inputs ...` carga un `meta.json` de prueba sin escribirlo, para probar estas pantallas.

**Eventos de temporada (hito 6c).** La fecha decide el evento con `DetectSeasonalEvent`, y `GameData::SeasonalConfig` devuelve la configuración "None" si el evento no está permitido (Navidad). En Halloween y Pascua:
- El título usa el logo del evento y suena `jingle.titleHalloween` o `jingle.titleEaster`. Son clips nuevos de `export_audio.py`.
- En Modes, el Daily muestra un aviso con los colores del evento si no se ha jugado hoy.
- En Halloween salen los avisos TRICK!/TREAT! durante la stage y el récord del Daily no se guarda (`preventHighScoreSaving`).
- En los resultados, el tesoro se llama como en el evento (calabazas o huevos).

`--date AAAA-MM-DD` fija la fecha del título, las temporadas y la seed del Daily. En PC, `DDVITA_WINDOW=960x544` abre la ventana al doble de la Vita para ver su espacio de 480×272. El título reduce el logo, y si hace falta lo sube, para que quepan el menú y el aviso de la partida guardada.

No hay selector de idioma: el original fuerza el inglés y cada idioma solo traduce unas 100 de las 1022 cadenas.

La sim solo informa cuando `Game::meta` apunta a algo: la stage en juego. Las repeticiones del guardado y las pruebas no la tocan, así que el resultado y el RNG no cambian. Con `--auto`, `--shot` o `--inputs` no se carga ni se guarda `meta.json`. Los logros se cuentan en memoria y salen en consola como `achievement: <nombre>`. Sin `achievements.json` la pantalla queda vacía y los avisos solo muestran el nombre interno.

`ddgame` sin seed abre el título. Con seed entra directo en una partida (pruebas). En PC, Esc es la pausa y Retroceso vuelve atrás.

Para pruebas, `--inputs teclas` pulsa una tecla cada 0,3 s: U D L R (cruceta), C confirmar, B atrás, M menú, S cambiar, P pasar, T chutar, Z descansar, X deshacer y `I` información y `.` nada. Por ejemplo, `ddgame data 2 0 0 --inputs C.M --shot pausa.bmp 2` captura la pausa.

Pruebas sin hardware:

```powershell
& $env:TEMP\ddvita-pc\savetest.exe data\gamedata.json 5000 3000 0    # guarda, recarga y repite tras cada stage
& $env:TEMP\ddvita-pc\audiotest.exe data 12345 15 stage.wav          # mezcla una stage a WAV y mide niveles
```

## Compilar

Las rutas de esta carpeta superan MAX_PATH para ninja; el directorio de build va en `%TEMP%`.

```powershell
# PC (MSYS2 ucrt64)
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -S port -B $env:TEMP\ddvita-pc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build $env:TEMP\ddvita-pc

# PS Vita: genera ddvita.vpk (VitaSDK con vitaGL, vitashark y mathneon)
$env:VITASDK = "C:\vitasdk"
$env:PATH = "C:\vitasdk\bin;C:\msys64\ucrt64\bin;$env:PATH"
cmake -S port -B $env:TEMP\ddvita-vita -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/vitasdk/share/vita.toolchain.cmake '-DCMAKE_CXX_FLAGS=-Wl,-q'
cmake --build $env:TEMP\ddvita-vita
```

La build de Vita deja `dist/ddvita.vpk` (sin datos). Los datos se copian aparte a `ux0:data/ddvita/`.

Con `-DDDVITA_PACK_DATA=ON` también prepara los datos con `tools/prepare_data.py`. Desde la copia del juego (`DDVITA_GAME_DIR`, por defecto la ruta de Steam) ejecuta solo los pasos cuya salida falta: extracción, gamedata, render, fuentes, textos y audio. Después deja en `dist/`:

| Archivo | Contenido |
|---|---|
| `ddvita.vpk` | Solo el juego, sin datos (~1 MB) |
| `ddvita_full.vpk` | Juego y datos en `app0:data`: se instala un solo archivo (~93 MB) |
| `ddvita_data.zip` | Carpeta `ddvita/` para extraer en `ux0:data/` si usas el VPK sin datos |

En la Vita se usa `ux0:data/ddvita` si tiene `gamedata.json`; si no, `app0:data`. Solo se reempaqueta cuando cambia el eboot o los datos. Con `-DDDVITA_PACK_DATA=OFF` se genera únicamente el VPK. **`ddvita_full.vpk` y `ddvita_data.zip` llevan datos de tu copia del juego: son para tu consola, no se comparten.**

Hay dos rodeos en el CMake de Vita:
- `vita/gap.ld` deja hueco tras el segmento de código. Sin él, cuando el código acaba justo antes de un límite de 64 KiB, `vita-elf-create` escribe sus tablas de imports encima del segmento de datos y se cuelga (heap corruption).
- El VPK se empaqueta a mano porque `vita_create_vpk` no entrecomilla rutas con espacios.

## Datos: instalador (hito 7)

El VPK no lleva nada del juego. Cada jugador genera los datos desde su propia copia de Steam con `instalar.bat`, en la raíz del repositorio:

```powershell
instalar.bat                                  # busca el juego en las bibliotecas de Steam
instalar.bat "D:\Juegos\Dungeon Deathball"    # o en la carpeta indicada
instalar.bat --vpk ruta\ddvita.vpk            # usa ese VPK para ddvita_full.vpk
```

El instalador necesita Python 3.11 o posterior. Pasos:
1. Crea un entorno propio en `%LOCALAPPDATA%\ddvita\venv` e instala `tools/requirements.txt` (UnityPy y Pillow con versión fija).
2. `tools/install.py` busca el juego en este orden: el argumento, `DDVITA_GAME_DIR`, y las bibliotecas de Steam (registro y `libraryfolders.vdf`).
3. Avisa si la build de Steam (`appmanifest_861330.acf`) no es la 20267473, que es con la que se hizo el port.
4. Ejecuta `prepare_data.py` y deja en `dist/`:
   - siempre, `ddvita_data.zip`;
   - y además `ddvita_full.vpk` si encuentra `ddvita.vpk` (en `--vpk`, en `dist/`, en la raíz o en la carpeta de arriba).

Cada paso de `prepare_data.py` tiene una versión en `STEPS`. Al cambiar un exportador, se sube su número y el paso se repite en la siguiente instalación. Lo demás no se rehace. Las versiones quedan en `data/version.json` junto con `format`.

El juego comprueba `format` al arrancar (`kDataFormat` en `app/game_app.cpp`, `DATA_FORMAT` en `prepare_data.py`). Si los datos faltan o son de un formato anterior, muestra un mensaje del sistema que explica qué hacer y se cierra: en la Vita con el diálogo de mensajes, en PC con una ventana. Con `--auto`, `--shot` o `--inputs` solo lo escribe en la consola. Sube `kDataFormat` y `DATA_FORMAT` a la vez cuando el juego necesite datos nuevos.

## Instalador en la Vita (hito 8)

Sin PC ni Python: el VPK genera los datos en la consola desde la copia de Steam del usuario.

1. Instala `ddvita.vpk` (sin datos del juego).
2. Copia de la carpeta del juego en Steam a `ux0:data/ddvita/game/`:
   - la carpeta `Dungeon Deathball_Data/` completa;
   - `UnityPlayer.dll` (de ella salen las cabeceras Vorbis del audio).
3. Abre el juego. Si `ux0:data/ddvita/version.json` falta o es de otra versión, muestra el progreso con el diálogo del sistema y escribe los datos en `ux0:data/ddvita/`. `version.json` se escribe al final, así que una instalación interrumpida se repite entera.
4. Al terminar, `ux0:data/ddvita/game/` se puede borrar. Hace falta otra vez solo si una actualización del port sube la versión de algún paso.

El código está en `src/install/`: lectura de SerializedFile/UnityFS (`unity_file`, `game_files`), typetrees de los MonoBehaviour desde `Managed/*.dll` (`dotnet`, `mono_types`), y los exportadores (`extract`, `export_data`, `export_render`, `export_fonts`, `export_audio`, `installer`). Las versiones de los pasos (`kVersion` en `installer.cpp`) van a la par con `STEPS` de `prepare_data.py`.

CMake descarga `stb_vorbis.c` y `stb_truetype.h` de un commit fijo de nothings/stb y comprueba su SHA256. La fuente del HUD (`vita/font/font.*`, la fuente por defecto de Pillow) es del port y va en el VPK como `app0:assets/font.*`.

`ddinstall` (solo PC) sirve para comparar con la salida de Python:

```powershell
$ddi = "$env:TEMP\ddvita-pc\ddinstall.exe"
& $ddi install "<ruta del juego>" $env:TEMP\ddi-out port\vita\font   # lo mismo que hace la Vita; imprime memoria por etapa
& $ddi pcmcheck "<ruta del juego>\Dungeon Deathball_Data" <wavs de referencia>   # decodifica los clips y los compara con WAV de FMOD
```

En PC tarda unos 7 s con un pico de 102 MB. La salida es idéntica a la de Python, salvo las tildes de algunas letras de font_s y font_m (stb_truetype no aplica el hinting de Pillow).

Pasos sueltos, para desarrollo:

```powershell
python -E -P tools\extract_assets.py "<ruta del juego>" extracted --only-data
python -E -P tools\export_gamedata.py extracted data\gamedata.json
```

## Verificación

> `tools/oracle/` es una herramienta local que **no está en el repositorio**. Sus stubs copian literalmente dos funciones del código descompilado, así que no se publica. Lo que sigue describe cómo se verificó el core.

`tools/oracle/run_oracle.ps1` compila **sin modificar** los `.cs` decompilados del generador (SessionGenerator, PerkManager, PlayerClasses, HumanPlayerConfig, TeamConfig, SeasonalEvents, Session...) sobre stubs mínimos de Unity. Los alimenta con el mismo `gamedata.json` y compara su salida, línea a línea, con `sessiondump`. Incluye un control negativo: dos seeds distintas tienen que dar salidas distintas.

```powershell
pwsh -File tools\oracle\run_oracle.ps1 -SessionDump $env:TEMP\ddvita-pc\sessiondump.exe -Count 600
```

Resultado actual: **619/619 partidas idénticas**. Cubre 600 seeds, incluidas 0, ±1, `int.MaxValue` e `int.MinValue`, en los modos Normal, Boss y Action, con los 19 equipos. Además, 19 retos diarios en fechas de Halloween, Pascua, Navidad y años bisiestos.

El RNG se comprobó aparte contra `System.Random` de .NET: 1.600 valores, incluidos rangos de más de `int.MaxValue`. Su algoritmo es idéntico al del mscorlib del juego (decompilado en `analysis/mono_Random.cs`).

Comprobación con el juego real: `sessiondump data\gamedata.json --daily <fecha UTC>` debe mostrar los mismos 16 reclutas y la misma primera stage que el reto diario de ese día en la versión de Steam.
