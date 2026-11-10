cmake_minimum_required(VERSION 3.16)
project(DungeonDeathballVita C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# nlohmann/json (MIT, single header) is not in the repository: use third_party/ if present,
# otherwise download the pinned release into the build directory.
set(JSON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party)
if(NOT EXISTS ${JSON_INCLUDE_DIR}/nlohmann/json.hpp)
    set(JSON_INCLUDE_DIR ${CMAKE_BINARY_DIR}/third_party)
    set(JSON_HPP ${JSON_INCLUDE_DIR}/nlohmann/json.hpp)
    set(JSON_SHA256 9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6)
    if(NOT EXISTS ${JSON_HPP})
        message(STATUS "Downloading nlohmann/json 3.11.3")
        file(DOWNLOAD https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp ${JSON_HPP}
            EXPECTED_HASH SHA256=${JSON_SHA256} STATUS JSON_STATUS)
        list(GET JSON_STATUS 0 JSON_CODE)
        if(NOT JSON_CODE EQUAL 0)
            file(REMOVE ${JSON_HPP})
            message(FATAL_ERROR "Could not download nlohmann/json: ${JSON_STATUS}. "
                "Put json.hpp 3.11.3 in port/third_party/nlohmann/ instead.")
        endif()
    endif()
endif()

# stb_vorbis and stb_truetype (public domain / MIT) for the data installer: same, pinned to a commit.
set(STB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party)
if(NOT EXISTS ${STB_INCLUDE_DIR}/stb/stb_vorbis.c OR NOT EXISTS ${STB_INCLUDE_DIR}/stb/stb_truetype.h)
    set(STB_INCLUDE_DIR ${CMAKE_BINARY_DIR}/third_party)
    set(STB_COMMIT d14d46f6b44456eb88beac58cba833a4135d4c89)
    foreach(entry "stb_vorbis.c=4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636"
                  "stb_truetype.h=ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab")
        string(REPLACE "=" ";" parts ${entry})
        list(GET parts 0 STB_FILE)
        list(GET parts 1 STB_SHA256)
        if(NOT EXISTS ${STB_INCLUDE_DIR}/stb/${STB_FILE})
            message(STATUS "Downloading stb/${STB_FILE}")
            file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/${STB_FILE} ${STB_INCLUDE_DIR}/stb/${STB_FILE}
                EXPECTED_HASH SHA256=${STB_SHA256} STATUS STB_STATUS)
            list(GET STB_STATUS 0 STB_CODE)
            if(NOT STB_CODE EQUAL 0)
                file(REMOVE ${STB_INCLUDE_DIR}/stb/${STB_FILE})
                message(FATAL_ERROR "Could not download stb/${STB_FILE}: ${STB_STATUS}. "
                    "Put it (commit ${STB_COMMIT}) in port/third_party/stb/ instead.")
            endif()
        endif()
    endforeach()
endif()

# The core must reproduce the original's float/double results exactly: never enable fast-math.
add_library(ddcore STATIC
    src/core/mono_random.cpp
    src/core/mono_string.cpp
    src/core/calendar.cpp
    src/core/gamedata.cpp
    src/core/session_generator.cpp
)
target_include_directories(ddcore PUBLIC src ${JSON_INCLUDE_DIR})
if(NOT MSVC)
    target_compile_options(ddcore PRIVATE -Wall -Wextra -Wno-psabi -fno-fast-math)
endif()

# Headless gameplay simulation (one stage: turns, AI, combat, ball, scoring, undo).
add_library(ddsim STATIC
    src/sim/run.cpp
    src/sim/field.cpp
    src/sim/actors.cpp
    src/sim/attack.cpp
    src/sim/ai.cpp
    src/sim/game.cpp
    src/sim/describe.cpp
)
target_link_libraries(ddsim PUBLIC ddcore)
if(NOT MSVC)
    target_compile_options(ddsim PRIVATE -Wall -Wextra -Wno-psabi -fno-fast-math)
endif()

# Renderer (fixed-function OpenGL: desktop GL through SDL2 on PC, vitaGL on the Vita), audio
# (software mixer + game sounds), UI and the game (screens and flow) shared by both platforms.
add_library(ddgamelib STATIC
    src/render/gfx.cpp
    src/render/assets.cpp
    src/render/stage_view.cpp
    src/audio/mixer.cpp
    src/audio/sound.cpp
    src/app/game_app.cpp
    src/app/help.cpp
    src/app/meta.cpp
    src/app/screens.cpp
    src/app/screens_help.cpp
    src/app/screens_meta.cpp
    src/app/settings.cpp
    src/app/save.cpp
    src/ui/text.cpp
    src/ui/ui.cpp
    src/ui/canvas.cpp
)
target_link_libraries(ddgamelib PUBLIC ddsim ddinstalllib)
if(NOT MSVC)
    target_compile_options(ddgamelib PRIVATE -Wall -Wextra -Wno-psabi)
endif()

# On-Vita installer: reads the user's own copy of the game (Unity files) and writes data/.
add_library(ddinstalllib STATIC
    src/install/unity_file.cpp
    src/install/game_files.cpp
    src/install/dotnet.cpp
    src/install/mono_types.cpp
    src/install/extract.cpp
    src/install/export_data.cpp
    src/install/export_render.cpp
    src/install/export_fonts.cpp
    src/install/export_audio.cpp
    src/install/installer.cpp
    src/install/stb_vorbis_impl.c
)
set_source_files_properties(src/install/stb_vorbis_impl.c PROPERTIES COMPILE_OPTIONS "-w") # third-party code
target_include_directories(ddinstalllib PUBLIC src ${JSON_INCLUDE_DIR})
target_include_directories(ddinstalllib PRIVATE ${STB_INCLUDE_DIR}) # stb/stb_truetype.h, stb/stb_vorbis.c
if(NOT MSVC)
    # no fused multiply-add: the arena matrices must round like the Python exporter's
    target_compile_options(ddinstalllib PRIVATE -Wall -Wextra -Wno-psabi -ffp-contract=off)
endif()

if(VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    set(VITA_APP_NAME "Dungeon Deathball")
    set(VITA_TITLEID "DDBV00001")
    set(VITA_VERSION "01.00")
    add_executable(ddvita src/app/platform_vita.cpp)
    target_link_libraries(ddvita PRIVATE ddgamelib
        vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub
        SceKernelDmacMgr_stub SceCommonDialog_stub SceGxm_stub SceDisplay_stub SceSysmodule_stub SceAppMgr_stub
        SceCtrl_stub SceAudio_stub ScePower_stub SceRtc_stub SceIme_stub SceLibKernel_stub
        pthread m c)
    # room after the text segment for the tables vita-elf-create appends (see vita/gap.ld)
    target_link_options(ddvita PRIVATE "-Wl,-T,${CMAKE_SOURCE_DIR}/vita/gap.ld")
    set_property(TARGET ddvita APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/vita/gap.ld)
    vita_create_self(eboot.bin ddvita UNSAFE)
    # VPK packed by hand: vita_create_vpk does not quote paths, and this project lives under a path
    # with spaces. LiveArea images are generated by tools/make_livearea.py (not game art).
    set(SCE_SYS ${CMAKE_SOURCE_DIR}/vita/sce_sys)
    set(VPK_FILES
        ${SCE_SYS}/icon0.png ${SCE_SYS}/livearea/contents/bg.png
        ${SCE_SYS}/livearea/contents/startup.png ${SCE_SYS}/livearea/contents/template.xml
        ${CMAKE_SOURCE_DIR}/vita/font/font.tex ${CMAKE_SOURCE_DIR}/vita/font/font.json)
    add_custom_command(OUTPUT ddvita.vpk
        COMMAND vita-mksfoex -s APP_VER=${VITA_VERSION} -s TITLE_ID=${VITA_TITLEID} ${VITA_APP_NAME} param.sfo
        COMMAND vita-pack-vpk -s param.sfo -b eboot.bin
            -a ${SCE_SYS}/icon0.png=sce_sys/icon0.png
            -a ${SCE_SYS}/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png
            -a ${SCE_SYS}/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png
            -a ${SCE_SYS}/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml
            -a ${CMAKE_SOURCE_DIR}/vita/font/font.tex=assets/font.tex
            -a ${CMAKE_SOURCE_DIR}/vita/font/font.json=assets/font.json
            ddvita.vpk
        DEPENDS eboot.bin-self ${CMAKE_CURRENT_BINARY_DIR}/eboot.bin ${VPK_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM)
    get_filename_component(PROJECT_ROOT ${CMAKE_SOURCE_DIR} DIRECTORY)
    add_custom_target(ddvita-vpk ALL DEPENDS ddvita.vpk
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_ROOT}/dist
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ddvita.vpk ${PROJECT_ROOT}/dist/ddvita.vpk
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM)

    # Optional (DDVITA_PACK_DATA=ON): game data from the user's copy (only the missing steps run)
    # packed into dist/ddvita_full.vpk (data inside) and dist/ddvita_data.zip (for ux0:data).
    option(DDVITA_PACK_DATA "Export the game data and pack it into dist/" OFF)
    set(DDVITA_GAME_DIR "C:/Program Files (x86)/Steam/steamapps/common/Dungeon Deathball" CACHE PATH "Game install folder")
    if(DDVITA_PACK_DATA)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_custom_target(ddvita-data ALL
            COMMAND Python3::Interpreter -E -P ${PROJECT_ROOT}/tools/prepare_data.py ${DDVITA_GAME_DIR} ${PROJECT_ROOT}
                --vpk ${CMAKE_CURRENT_BINARY_DIR}/ddvita.vpk --dist ${PROJECT_ROOT}/dist
            WORKING_DIRECTORY ${PROJECT_ROOT}
            USES_TERMINAL
            VERBATIM)
        add_dependencies(ddvita-data ddvita-vpk)
    endif()
endif()

if(NOT VITA)
    # the windowed game needs SDL2; without it only the command-line tools are built
    find_package(SDL2 QUIET)
    find_package(OpenGL QUIET)
    if(SDL2_FOUND)
    if(WIN32)
        target_link_libraries(ddgamelib PUBLIC SDL2::SDL2 opengl32)
    else()
        if(TARGET OpenGL::GL)
            target_link_libraries(ddgamelib PUBLIC SDL2::SDL2 OpenGL::GL)
        else()
            target_link_libraries(ddgamelib PUBLIC SDL2::SDL2 GL) # no GL dev package: SDL_opengl.h has the declarations
        endif()
    endif()
    add_executable(ddgame src/app/main.cpp)
    target_link_libraries(ddgame PRIVATE ddgamelib SDL2::SDL2main SDL2::SDL2)
    else()
        message(STATUS "SDL2 not found: ddgame is not built")
    endif()
    if(SDL2_FOUND AND MINGW)
        # only SDL2.dll next to the exe (copied by the build)
        target_link_options(ddgame PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
        add_custom_command(TARGET ddgame POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:ddgame>)
    endif()

    add_executable(sessiondump tools/sessiondump.cpp)
    target_link_libraries(sessiondump PRIVATE ddcore)
    add_executable(ddplay tools/ddplay.cpp)
    target_link_libraries(ddplay PRIVATE ddsim)
    add_executable(simtest tools/simtest.cpp)
    target_link_libraries(simtest PRIVATE ddsim)
    add_executable(savetest tools/savetest.cpp src/app/save.cpp)
    target_link_libraries(savetest PRIVATE ddsim)
    add_executable(metatest tools/metatest.cpp src/app/meta.cpp src/app/save.cpp)
    target_link_libraries(metatest PRIVATE ddsim)
    add_executable(audiotest tools/audiotest.cpp src/audio/mixer.cpp src/audio/sound.cpp)
    target_link_libraries(audiotest PRIVATE ddsim)
    add_executable(ddinstall tools/ddinstall.cpp)
    target_link_libraries(ddinstall PRIVATE ddinstalllib)
    if(MINGW)
        target_link_options(ddinstall PRIVATE -static)
        target_link_options(audiotest PRIVATE -static)
        target_link_options(savetest PRIVATE -static)
        target_link_options(sessiondump PRIVATE -static)
        target_link_options(ddplay PRIVATE -static)
        target_link_options(simtest PRIVATE -static)
    endif()
endif()
