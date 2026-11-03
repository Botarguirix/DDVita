# Dungeon Deathball → PS Vita: análisis y plan de port

## 1. Origen (copia de Steam del usuario)

| Dato | Valor |
|---|---|
| Motor | Unity **2020.3.37f1**, renderer integrado (sin URP/HDRP) |
| Backend | **Mono** (`MonoBleedingEdge/`, sin `GameAssembly.dll`) → C# decompilable |
| Tamaño | 162 MB / 200 archivos; 11 escenas (`level0..10`) |
| Plataformas previstas en el código | Windows, Switch (`Rewired.Platforms.Switch`), Android (`Rewired_Android`, `ChangeCameraForOrientation`) |

## 2. Herramientas y salidas

| Ruta | Contenido |
|---|---|
| `tools/ilspycmd/` | ilspycmd 9.1 (corre con el runtime .NET 8: `dotnet tools\ilspycmd\pkg\tools\net8.0\any\ilspycmd.dll`) |
| `tools/asset_inventory.py` | Inventario de recursos → `analysis/asset_inventory.json` |
| `tools/extract_assets.py` | Extractor completo (ver cabecera del script) |
| `decomp/` | Proyectos C# decompilados (ver §3) |
| `extracted/textures` | 364 PNG (Texture2D) |
| `extracted/sprites` | 699 PNG + `sprites.json` (rect, pivot, ppu, textura) |
| `extracted/audio` | 489 WAV (35 min; música 44.1 kHz estéreo; origen Vorbis) |
| `extracted/configs/<Clase>/` | 1.205 ScriptableObjects a JSON (balance completo del juego) |
| `extracted/localization` | 52 tablas: FrontEnd, Gameplay, General, Help, Achievements × 8 idiomas |
| `extracted/pptr_index.json` | `archivo:path_id → [tipo, nombre]`, para resolver referencias en los JSON |

Reextraer: `python -E -P tools\extract_assets.py "<ruta juego>" extracted [--only-data]`

> ⚠️ `decomp/` y `extracted/` son material con copyright del autor (Matt Glanville). Solo sirven de referencia local; **no se distribuyen**. El port final debe extraer los recursos de la copia del usuario en la instalación.

### Notas técnicas de extracción
- Los builds de jugador no incluyen typetrees. Se generan desde los DLL con `TypeTreeGeneratorAPI`.
- `LocalizedString` usa `[SerializeReference]` y UnityPy no resuelve su registro. El extractor elimina el nodo `ManagedReferencesRegistry` (siempre va al final) y lee el resto. Solo se pierde `m_LocalVariables`, que está vacío en lo revisado.
- El generador de typetrees declara los `string[]` como `string`, y eso desalineaba todo lo que venía después. El extractor los corrige a `vector`. Con eso se leen completos `HumanPlayerConfig`, Cinemachine y Rewired.
- Se exportan también los componentes de escena de las clases del juego que tienen como mucho 20 instancias (`extracted/scene_components/`: `Teams`, `PlayerClasses`, `AppDeathball`, `Configs`...).
- `tools/export_gamedata.py` construye `data/gamedata.json`, con las referencias resueltas a IDs `archivo:path_id`. Es lo que lee el core.
- Con rutas largas de Windows se usa el prefijo `\\?\`.

## 3. Código

| Ensamblado | Archivos | Líneas | Uso |
|---|---|---|---|
| `_Project.Runtime` | 433 | 31.835 | **Todo el juego** |
| `com.mattglanville.eggtools-base` | 104 | 5.151 | Pantallas, localización, utilidades |
| `com.mattglanville.eggtools-menus` | 41 | 2.107 | Sistema de menús (`MenuObject`) |
| `com.mattglanville.eggtools-achievements` | 5 | 458 | Logros |
| `Assembly-CSharp` | 31 | 5.667 | Casi todo adaptadores de Rewired/ES3 → descartable |
| `Flashback94` / `CameraShake` | 6 | 1.162 | Filtro PS1 (cuantización de color) y temblor de cámara |

**Uso de APIs de Unity:** 0 física, 0 raycasts, 0 NavMesh. 150 corrutinas (secuencias de animación → máquinas de estados o tareas). 55 `Instantiate`. 35 referencias a Cinemachine. 41 al `Animator`.

### Sistemas núcleo (lógica pura de cuadrícula y fácil de portar)
- **Generación de partida:** `SessionGenerator`. 4 Trials × 3 Stages, `System.Random(seed)`, presupuesto de enemigos, muros, peligros, gemas y bosses. Toma los parámetros de `SessionConfig` y `GameModeConfig`.
- **IA enemiga:** `EnemyActionPlanner`. Comportamientos Random, ChaseBall, ChasePlayers, AvoidPlayers, BlockBall y Static. Primero ataca, luego se mueve, luego huye de los peligros.
- **Turnos y estado:** `GameController`, `GameFlow`, `EnemyManager`, `PlayerManager`, `FieldManager`, `FieldTile`.
- **Combate y balón:** `Attack*`, `Ball`, `BallModifier`, `Knockback`, `Stun`, `Hazard`, `Treasure`.
- **Progresión:** `PerkManager`, `PerkChain`, `PlayerStatUpgrader`, `Teams`, `TeamTrait`, `Unlocks`, `LeagueManager`.
- **Deshacer:** `UndoManager` y `Undo*`.
- **Guardado:** `SaveManager` (Easy Save 3) → reemplazar por un formato binario o JSON propio en `ux0:data/`.

### Determinismo (importante para seeds y reto diario)
- Hay que replicar **exactamente** `System.Random` de Mono (algoritmo sustractivo de Knuth, igual que el reference source de .NET Framework) en C/C++. Se valida generando la misma partida con la misma seed en PC y en Vita.
- Seed del reto diario: `(fecha_estandarizada + plataforma).GetHashCode()`. Hay que replicar el `String.GetHashCode` no aleatorizado de Mono y comprobarlo con un valor conocido. Si se usa `"Steam"` como plataforma, sería **el mismo reto diario que en PC**.
- `UnityEngine.Random` se usa solo en 9 sitios (cosmético).

## 4. Recursos y presupuesto en Vita

| Recurso | Origen | En Vita |
|---|---|---|
| Texturas | 375; la mayoría RGBA32/RGB24 ≤ 512² (más un 1024² de rayos) | PNG→RGBA8 o PVRTC/paletizado; filtro point; VRAM holgada |
| Sprites | 699 (pixel art pequeño) | Atlas propios generados en la instalación |
| Mallas | 21 (quads, marcadores, meshes combinados del escenario) | Exportar a formato simple propio (OBJ o binario) |
| Shaders | Standard, Legacy Diffuse/VertexLit, Unlit, Sprites, Particles, Flashback94 | vitaGL con pipeline fijo + 1 shader de cuantización |
| Audio | 489 clips, 35 min | Música → OGG 32–44 kHz en streaming; SFX → WAV/ADPCM en RAM |
| Partículas | 59 `ParticleSystem` | Sistema propio sencillo (sangre, explosiones, rayos) |
| Animación | 154 clips, 29 controladores y 27 override controllers (sprites por frames) | Tablas de frames a partir de `SimpleAnimationConfig` y los clips |
| UI | ~2.300 `Image`, ~890 `Text`, 233 `MenuObject` | **Lo más laborioso:** rehacer en código con layout fijo a 960×544 |
| Cámaras | Cinemachine (88 vcams, dolly por el campo) | Cámaras fijas con interpolación y un dolly lineal |
| Cinemáticas | 4 `TimelineAsset` (intro, nueva partida, final, trial completo) | Secuencias scriptadas |
| Fuentes | 18 `FontConfig` + CJK | Fuente bitmap latina + fuente CJK (por ejemplo, la del sistema vía `sceFont` o una bitmap) |

## 5. Arquitectura propuesta

```
DungeonDeathballVita/
  installer/   (PC) script Python: extrae de la copia de Steam → data/ (atlas, ogg, json/bin)
  src/
    core/      lógica 1:1 portada del C# (session, field, ai, combat, perks, undo, rng)
    render/    vitaGL: billboards, tiles, RT a baja resolución + cuantización (Flashback94)
    ui/        menús/popups para mando (L/R para pestañas, ○/× según región)
    audio/     SDL2_mixer u OpenAL-soft: música en streaming y SFX
    platform/  guardado en ux0:data, entrada, trofeos opcionales
  CMakeLists.txt (VitaSDK)
```

- Lenguaje: C++17 sobre VitaSDK, con vitaGL, SDL2 y SDL2_mixer.
- Render: pantalla a 480×272 (o la resolución nativa del filtro) escalada a 960×544. Tiles en 3D sencillo y personajes billboard con `SetBillboardFlipByCameraPosition`.
- El core se escribe sin dependencias de render, así se puede probar en PC (build SDL2 en Windows) contra las seeds del juego original.

## 6. Hitos

1. ✅ **Core determinista** (2026-10-31): RNG de Mono, hash de texto, eventos de temporada y `SessionGenerator` en C++ ([port/README.md](port/README.md)). Verificado contra el C# original sin modificar: 619/619 partidas idénticas. Compila para Vita. Pendiente: la comparación visual con el reto diario en PC. `FieldGenerator` (crear las piezas de una stage a partir de los datos generados) pasa al hito 2.
2. ✅ **Simulación jugable en PC** (2026-10-31): turnos, IA enemiga, combate, balón, peligros, tesoros (con Halloween y Pascua), undo, puntuación y oro, traducidos clase por clase (`port/src/sim`). Se juega en consola con `ddplay` (render ASCII provisional). `simtest`: unas 3.000 partidas aleatorias por todos los modos, trials y temporadas, sin romper invariantes y deterministas. Compila para Vita. No hay oráculo automático (el C# depende de MonoBehaviour): falta comparar `ddplay daily` con el reto diario de Steam. Pendiente para hitos posteriores: el reinicio tras derrota (`SessionStageBackup`) y la capa meta entre stages (mejoras, ball mods, fichajes). La ventana SDL2 pasa al hito 3.
3. ✅ **Render fiel en PC** (2026-10-31): `ddgame` (SDL2 + OpenGL fijo, compatible con vitaGL). Incluye arenas S/M/L con los cambios de textura por trial, billboards con la geometría de los prefabs, casillas y overlays, flechas de intención 9-slice, sprites de jugador compuestos (con expresiones), animaciones de sprites, partículas básicas y reproducción temporizada de los eventos de la simulación. El "filtro PS1" del juego (Flashback94) es solo el downsample a 640×360 con point sampling, sin cuantizar color; eso es lo que hace el render. Aproximaciones pendientes de cotejar con capturas del original: la cámara (dolly + group composer), la animación de los marcadores de casilla, los clips de animación de los enemigos (se eligen por nombre: `_Idle`/`_Warmup`/`_Attack`) y las partículas. El HUD usa una fuente provisional; la UI de verdad va en el hito 5.
4. ✅ **Port a Vita** (2026-11-02): capa de plataforma (SDL2 en PC, vitaGL/SceCtrl/SceAudioOut en Vita) con un bucle de juego común. Genera `ddvita.vpk` (DDBV00001, unos 1,1 MB, sin assets del juego). El render en Vita va a un FBO de 640×360.
   - Audio: exportador a IMA ADPCM, mezclador propio a 48 kHz, cues, Jukebox, jingles, voces y sonidos por evento. Los volúmenes por grupo son los del AudioMixer original. Verificado con `audiotest`: sin saturar, alrededor de −19 dBFS RMS.
   - Guardado: un registro de entradas que se repite con la sim determinista. Verificado con `savetest`: 839 guardados en 3.000 partidas, todos repetidos idénticos.
   - **Sin probar en hardware:** no hay Vita ni emulador en este equipo, así que el arranque, el rendimiento real (lectura de fps/ms con Start) y la latencia de audio están por medir.
   - Pendiente: la multitud (CrowdAudio) y los sonidos de menús, que llegan con la UI del hito 5.
5. **UI completa:** menús, equipo, perks, tienda, resultados, How to Play, opciones.
   - 5a ✅ (2026-11-02, solo probado en PC): título, modo y equipo, panel de inicio de stage con elección de balón, pausa (reanudar, reiniciar stage, opciones, salir), derrota con reinicio, resultados, gestión del equipo (mejoras y reclutas, lógica en la sim y en el guardado), "Keep Going?" / Save & Quit, final y opciones de volumen. Usa las fuentes pixel y los textos en inglés del juego (`data/loc`, `data/render/font_s/m/l`).
   - Guardado v2: cada stage guarda balón, entradas y compras. Se carga una vez (se borra al continuar) y solo se escribe con Save & Quit. Verificado con `savetest`: 600 partidas, 209 guardados y 95 compras repetidos idénticos.
   - Simplificado por ahora: todos los modos y equipos desbloqueados (hito 6), sin la opción de intercambiar posiciones y sin sonido de multitud.
   - 5b ✅ (2026-11-03, solo probado en PC): How to Play (15 páginas del original y una de controles propia, desde el título y la pausa) y ventana de información en juego con R (enemigo, jugador, balón y casillas). Datos en `data/help.json` (`tools/export_help.py`). El orden de las páginas no está en los datos extraídos y se fijó a mano.
6. **Meta:** modos (Daily, Judgment, Action, Boss Rush), logros a trofeos o locales, eventos de temporada, localización (8 idiomas).
   - 6a ✅ (2026-11-03, solo probado en PC): guardado persistente `meta.json`, las estadísticas de GlobalStatTracker, los 84 logros como logros locales (el homebrew no puede dar trofeos), el aviso "Achievement Unlocked!" y la pantalla Stats & Achievements del título. La sim avisa a través de `Game::meta`. Las repeticiones del guardado no lo usan, así que la sim sigue siendo determinista. Datos en `data/achievements.json` (`tools/export_achievements.py`).
   - 6b ✅ (2026-11-03, solo probado en PC): llaves de calavera al completar cada trial (por equipo, sin repetir), aviso "Skull Keys Acquired!" antes de "Keep Going?", equipos bloqueados que se compran con llaves (y algunos con un logro), selección de equipo bloqueada hasta la primera llave, modos bloqueados por número de logros y logros concretos, avisos de modo y de música desbloqueados en el título y packs de música B-D en el shuffle. Todo se guarda en `meta.json`. Los datos ya estaban en `gamedata.json`.
   - 6c ✅ (2026-11-03, solo probado en PC): eventos de temporada según la fecha. Halloween (días 298-305) y Pascua cambian el logo del título y suena su jingle. El reto diario muestra un aviso con los colores del evento si no se ha jugado hoy. En Halloween salen los avisos TRICK!/TREAT! en juego, el tesoro se llama calabazas o huevos en los resultados y el récord del Daily no se guarda. Navidad sigue desactivada, como en el original (`allowSeason=0`). `--date AAAA-MM-DD` fija la fecha para probar.
   - Localización: no hay selector de idioma. El original fuerza el inglés (`AppDeathball.forceEnglishLanguage` y `LoadingScreen`) y cada idioma solo tiene unas 100 de las 1022 cadenas traducidas, así que el port sigue en inglés.
7. **Instalador** (extracción desde la copia del usuario) y empaquetado VPK.
   - ✅ (2026-11-03, probado en PC con la copia de Steam): `instalar.bat` crea su propio venv con UnityPy y Pillow de versión fija y busca el juego en las bibliotecas de Steam. Avisa si la build de Steam no es la probada (20267473) y genera `ddvita_data.zip`, más `ddvita_full.vpk` si tiene `ddvita.vpk`. Cada paso de `prepare_data.py` tiene versión (`data/version.json`), así que una actualización solo rehace lo que cambió. Si faltan los datos o son antiguos, el juego lo explica con un mensaje del sistema (diálogo de la Vita, sin probar en hardware).

8. **Instalador en la Vita** ✅ (2026-11-03, probado en PC; en la Vita falta probar tiempo, memoria y el diálogo de progreso): el usuario copia `Dungeon Deathball_Data/` y `UnityPlayer.dll` de su copia de Steam a `ux0:data/ddvita/game/`, y el VPK genera `ux0:data/ddvita/` en el primer arranque, sin PC ni Python. El VPK público sigue sin datos del juego. Código en `port/src/install/`, con una herramienta `ddinstall` en PC para comparar su salida con la de los scripts de Python.
   - Lectura de Unity: SerializedFile v22 y bundles UnityFS con LZ4 (los de textos). 27 de los 37 archivos no traen typetrees:
     - los tipos del motor salen de `engine_types.inc`, generado por `tools/gen_engine_types.py` a partir de la base de tipos de UnityPy (tpk de AssetRipper, MIT): son metadatos de Unity 2020.3, no del juego;
     - los de los MonoBehaviour se generan en la consola leyendo los ensamblados .NET del propio juego (`Managed/*.dll`), como hace TypeTreeGenerator. Así el código no lleva nada derivado del juego.
   - Texturas: RGBA32, RGB24, ARGB4444, Alpha8 y DXT1 (las crunched solo están en texturas que no se usan).
   - Audio: los clips son Vorbis dentro de FSB5. Las 6 cabeceras de configuración Vorbis que usan (identificadas por CRC32) se buscan en la `UnityPlayer.dll` del usuario, y se decodifican con stb_vorbis.
   - Fuentes: los TTF del juego se rasterizan con stb_truetype. CMake descarga stb_vorbis y stb_truetype (dominio público) de un commit fijo y comprueba su SHA256; no se suben al repositorio.
   - En la Vita, el progreso se muestra con el diálogo del sistema (no hace falta fuente propia). La fuente del HUD es del port (`port/vita/font`) y va dentro del VPK.
   - Resultado en PC (`ddinstall install`, 6,7 s, pico de 102 MB): gamedata, textos, ayuda, logros, audio.json y las 358 texturas usadas son idénticos a la salida de Python; el PCM queda a ±2 LSB del de FMOD. font_l es idéntica; font_s y font_m solo difieren en las tildes de algunas letras compuestas (stb respeta el diseño, Pillow las separa por el hinting).
   - De paso: los espectadores y el Skull Lord (script Billboard) ahora miran a la cámara (render v2).

## 7. Riesgos

| Riesgo | Mitigación |
|---|---|
| Volumen de UI (la mitad del código es presentación) | Priorizar flujos del juego y simplificar animaciones de menús |
| Paridad exacta del RNG y del hash de strings | Pruebas con seeds conocidas desde el inicio (hito 1) |
| Corrutinas y timing de las secuencias | Sistema de "acciones en cola" con duraciones de `TimingConfig` |
| Fuentes CJK y memoria | Cargar solo el idioma activo |
| Datos parciales (`HumanPlayerConfigMale`, vcams) | Completar a mano a partir del código y las escenas |
