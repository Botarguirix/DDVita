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
python -E -P tools\make_font.py data\render
& $env:TEMP\ddvita-pc\ddgame.exe data [seed|daily] [modo] [equipo]
```

Controles: con las flechas mueves el cursor y con Enter/Espacio haces la acción de esa casilla. Q pasa o coge el balón, E chuta, R descansa, U deshace, Tab cambia de jugador, Y/N responde a la sugerencia de undo y F1 alterna log → log + rendimiento → nada. Con mando: cruceta, A, X, Y, B, Back y L/R.

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

**Guardado.** `run.json` guarda la seed, el modo, el equipo, la fecha y los índices de comando de cada stage superada. Las respuestas al undo van como −1 (sí) y −2 (no). Al continuar, la simulación determinista repite esas stages sin pantalla y la partida sigue al principio de la stage en curso. Si el registro ya no se puede repetir (por ejemplo, con otro `gamedata.json`), esa partida empieza de nuevo. Al perder o terminar, el guardado se borra. `ddgame` continúa el guardado por defecto (`continue`); pon una seed para empezar otra partida.

**Vita.**
- Los datos van en `ux0:data/ddvita/` (`gamedata.json`, `render/`, `audio/`); el guardado también.
- vitaGL compila sus shaders al arrancar, así que hace falta `ur0:data/libshacccg.suprx`, como en cualquier homebrew con vitaGL.
- Controles: cruceta o stick para el cursor, Cruz para la acción de la casilla (y "sí"), Círculo para descansar (y "no"), Cuadrado para pasar o coger, Triángulo para chutar, Select para deshacer, L/R para cambiar de jugador y Start para log/rendimiento.
- El VPK solo lleva `eboot.bin` y la LiveArea, que generan imágenes originales con `tools/make_livearea.py`: ningún asset del juego.

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

La build de Vita también prepara los datos con `tools/prepare_data.py`. Desde la copia del juego (`DDVITA_GAME_DIR`, por defecto la ruta de Steam) ejecuta solo los pasos cuya salida falta: extracción, gamedata, render, fuente y audio. Después deja en `dist/`:

| Archivo | Contenido |
|---|---|
| `ddvita.vpk` | Solo el juego, sin datos (~1 MB) |
| `ddvita_full.vpk` | Juego y datos en `app0:data`: se instala un solo archivo (~93 MB) |
| `ddvita_data.zip` | Carpeta `ddvita/` para extraer en `ux0:data/` si usas el VPK sin datos |

En la Vita se usa `ux0:data/ddvita` si tiene `gamedata.json`; si no, `app0:data`. Solo se reempaqueta cuando cambia el eboot o los datos. Con `-DDDVITA_PACK_DATA=OFF` se genera únicamente el VPK. **`ddvita_full.vpk` y `ddvita_data.zip` llevan datos de tu copia del juego: son para tu consola, no se comparten.**

Hay dos rodeos en el CMake de Vita:
- `vita/gap.ld` deja hueco tras el segmento de código. Sin él, cuando el código acaba justo antes de un límite de 64 KiB, `vita-elf-create` escribe sus tablas de imports encima del segmento de datos y se cuelga (heap corruption).
- El VPK se empaqueta a mano porque `vita_create_vpk` no entrecomilla rutas con espacios.

## Datos

```powershell
python -E -P tools\extract_assets.py "<ruta del juego>" extracted --only-data
python -E -P tools\export_gamedata.py extracted data\gamedata.json
```

## Verificación

`tools/oracle/run_oracle.ps1` compila **sin modificar** los `.cs` decompilados del generador (SessionGenerator, PerkManager, PlayerClasses, HumanPlayerConfig, TeamConfig, SeasonalEvents, Session...) sobre stubs mínimos de Unity. Los alimenta con el mismo `gamedata.json` y compara su salida, línea a línea, con `sessiondump`. Incluye un control negativo: dos seeds distintas tienen que dar salidas distintas.

```powershell
pwsh -File tools\oracle\run_oracle.ps1 -SessionDump $env:TEMP\ddvita-pc\sessiondump.exe -Count 600
```

Resultado actual: **619/619 partidas idénticas**. Cubre 600 seeds, incluidas 0, ±1, `int.MaxValue` e `int.MinValue`, en los modos Normal, Boss y Action, con los 19 equipos. Además, 19 retos diarios en fechas de Halloween, Pascua, Navidad y años bisiestos.

El RNG se comprobó aparte contra `System.Random` de .NET: 1.600 valores, incluidos rangos de más de `int.MaxValue`. Su algoritmo es idéntico al del mscorlib del juego (decompilado en `analysis/mono_Random.cs`).

Comprobación con el juego real: `sessiondump data\gamedata.json --daily <fecha UTC>` debe mostrar los mismos 16 reclutas y la misma primera stage que el reto diario de ese día en la versión de Steam.
