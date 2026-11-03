# DDVITA: trabajo compartido

Objetivo: conservar y continuar el avance existente de Claude. La preparación del repositorio no añade funciones ni modifica la lógica del juego.

## Flujo de trabajo

- Revisar el estado local antes de cambiar archivos; no sobrescribir cambios de otro agente.
- Trabajar en ramas separadas, por ejemplo `claude/descripcion` y `codex/descripcion`.
- Sincronizar la rama principal antes de empezar y presentar cambios mediante pull requests.
- Indicar qué cambió, qué se verificó y qué queda pendiente. Las pruebas anteriores documentadas por Claude no equivalen a nuevas verificaciones.
- Los datos privados y los resultados de compilación se generan por separado en cada equipo.

## Alcance de la subida

Excluir siempre `data/`, `decomp/`, `extracted/`, `analysis/`, `dist/`, guardados, credenciales y herramientas binarias. No subir paquetes con datos del juego, aunque sean de uso personal.

Antes de incorporar código o dependencias, comprobar su procedencia y permiso de distribución. El usuario confirmó el 2 de noviembre de 2026 que tiene permiso del autor para compartir el código del avance existente. Esa confirmación no incluye distribuir los datos del juego. No eliminar avisos de copyright ni licencias para intentar hacer publicable un archivo.

La dependencia local `port/third_party/nlohmann/json.hpp` queda fuera de la subida solicitada; una copia del proyecto sin ella necesitará obtener la dependencia por separado antes de compilar. No se declara una licencia nueva para el trabajo existente.
