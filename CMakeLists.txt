cmake_minimum_required(VERSION 3.11)

project(segarally)

# Always optimise: psp-cmake leaves CMAKE_BUILD_TYPE empty, which means -O0. -O2 for every build type.
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set(CMAKE_C_FLAGS${cfg} "${CMAKE_C_FLAGS${cfg}} -O2")
    set(CMAKE_CXX_FLAGS${cfg} "${CMAKE_CXX_FLAGS${cfg}} -O2")
endforeach()

file(GLOB MENU_SOURCES ${CMAKE_SOURCE_DIR}/src/menu_*.c)

add_executable(${PROJECT_NAME}
    src/main.c
    src/sat.c
    src/satlib.c
    src/sat_boot.c
    src/sat_save.c
    src/satmenu.c
    src/vdpr.c
    ${MENU_SOURCES}
    src/render.c
    src/car.c
    src/event.c
    src/coli.c
    src/hud.c
    src/drive.c
    src/camera.c
    src/pmap.c
    src/hudgen.c
    src/spray.c
    src/opp.c
    src/ending_engine.c
    src/perf.c
    src/devauto.c
    src/devauto_menu.c
    src/play.c
    src/m68k.c
    src/scsp.c
    src/snd.c
    src/snd_lib.c
    src/sndev.c
    src/snd_psp.c
    src/bgm.c
    src/cdda.c
    src/sndlog.c
)

# The game reads (and saves into) ms0:/PSP/GAME/SegaRallyPSP/data.  -DSEGA_BASE=... (tools/run_ppsspp.ps1 -StickName) overrides it.
if(NOT SEGA_BASE)
    set(SEGA_BASE "ms0:/PSP/GAME/SegaRallyPSP/data")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE SEGA_BASE="${SEGA_BASE}")

target_compile_options(${PROJECT_NAME} PRIVATE -O2 -G0 -Wall -fno-strict-aliasing -fwrapv)
set_source_files_properties(src/m68k.c PROPERTIES COMPILE_DEFINITIONS M68K_NO_WH)      # the write hash is for the tests

target_link_libraries(${PROJECT_NAME} PRIVATE
    pspgum
    pspgu
    pspge
    pspdebug
    pspdisplay
    pspctrl
    psppower
    pspaudio
    psprtc
    pspmp3
    psputility
    m
)

# Optional XMB art lives in pbp/ (ICON0.png, PIC0.png, PIC1.png, SND0.AT3); missing files are skipped.
function(pbp_file out_var filename)
    if(EXISTS ${CMAKE_SOURCE_DIR}/pbp/${filename})
        set(${out_var} ${CMAKE_SOURCE_DIR}/pbp/${filename} PARENT_SCOPE)
    else()
        set(${out_var} NULL PARENT_SCOPE)
    endif()
endfunction()

pbp_file(PBP_ICON ICON0.png)
pbp_file(PBP_PREVIEW PIC0.png)
pbp_file(PBP_BACKGROUND PIC1.png)
pbp_file(PBP_MUSIC SND0.AT3)

create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH ${PBP_ICON}
    BACKGROUND_PATH ${PBP_BACKGROUND}
    PREVIEW_PATH ${PBP_PREVIEW}
    MUSIC_PATH ${PBP_MUSIC}
    TITLE "Sega Rally PSP"
    VERSION 01.00
    # MEMSIZE 1 unlocks the extra RAM on PSP-2000/3000 (the PSP-1000 has less memory and has not been tried).
    MEMSIZE 1
)
