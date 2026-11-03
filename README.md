# DDVITA

Base compartida del avance existente de Claude para Dungeon Deathball en PS Vita, con código C++17, simulación, render, audio, guardado y herramientas locales de preparación.

Consulta [las instrucciones del proyecto](port/README.md) para conocer su estado y compilar, y [COLLABORATION.md](COLLABORATION.md) para coordinar cambios entre Claude y Codex.

Este repositorio no incluye el juego original, datos extraídos, código C# descompilado, guardados, binarios ni paquetes de distribución. Cada usuario debe proporcionar su propia copia y generar los datos localmente. El permiso para compartir el código existente fue confirmado por el usuario; no se aplica una licencia adicional.

La cabecera de terceros `nlohmann/json.hpp` no se incluye: debe obtenerse por separado y colocarse en `port/third_party/nlohmann/json.hpp`, conservando su licencia. Las herramientas Python requieren sus dependencias locales (entre ellas Pillow, UnityPy y TypeTreeGeneratorAPI según el paso).

Los resultados de pruebas descritos en `port/README.md` son los documentados en el avance previo; esta preparación del repositorio no vuelve a certificar compilación ni ejecución en una consola.
