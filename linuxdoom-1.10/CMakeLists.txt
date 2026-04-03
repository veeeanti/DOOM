cmake_minimum_required(VERSION 3.16)

project(linuxdoom VERSION 1.10.0 LANGUAGES C CXX)

option(DOOM_ENABLE_DISCORD_RPC "Enable Discord Rich Presence integration" ON)

set(DOOM_CORE_SOURCES
    am_map.c
    d_items.c
    d_main.c
    d_net.c
    doomdef.c
    doomstat.c
    dstrings.c
    f_finale.c
    f_wipe.c
    g_game.c
    hu_lib.c
    hu_stuff.c
    info.c
    m_argv.c
    m_bbox.c
    m_cheat.c
    m_fixed.c
    m_menu.c
    m_misc.c
    m_random.c
    m_swap.c
    p_ceilng.c
    p_doors.c
    p_enemy.c
    p_floor.c
    p_inter.c
    p_lights.c
    p_map.c
    p_maputl.c
    p_mobj.c
    p_plats.c
    p_pspr.c
    p_saveg.c
    p_setup.c
    p_sight.c
    p_spec.c
    p_switch.c
    p_telept.c
    p_tick.c
    p_user.c
    r_bsp.c
    r_data.c
    r_draw.c
    r_main.c
    r_plane.c
    r_segs.c
    r_sky.c
    r_things.c
    s_sound.c
    sounds.c
    st_lib.c
    st_stuff.c
    tables.c
    v_video.c
    w_wad.c
    wi_stuff.c
    z_zone.c
    i_main.c
)

if(WIN32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/win32/doom.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/doom.rc
        @ONLY
    )

    set(DOOM_PLATFORM_SOURCES
        win32/i_video_win32.c
        win32/i_system_win32.c
        win32/i_sound_win32.c
        win32/ogg_temp.c
        win32/stb_vorbis.c
        ${CMAKE_CURRENT_BINARY_DIR}/doom.rc
    )
else()
    set(DOOM_PLATFORM_SOURCES
        i_video.c
        i_system.c
        i_sound.c
        i_net.c
    )
endif()

if(DOOM_ENABLE_DISCORD_RPC)
    list(APPEND DOOM_PLATFORM_SOURCES win32/discord_rpc_win32.c)
endif()

add_executable(linuxxdoom
    ${DOOM_CORE_SOURCES}
    ${DOOM_PLATFORM_SOURCES}
)

target_include_directories(linuxxdoom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    set_target_properties(linuxxdoom PROPERTIES OUTPUT_NAME doom)
    set_target_properties(linuxxdoom PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)

    if(MSVC)
        target_link_options(linuxxdoom PRIVATE
            "$<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>"
            "$<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>"
            "$<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>"
        )
    endif()
endif()

if(WIN32)
    target_compile_definitions(linuxxdoom PRIVATE _CRT_SECURE_NO_WARNINGS LINUX _WIN32 R_OK=4)
    target_compile_definitions(linuxxdoom PRIVATE strcasecmp=_stricmp strncasecmp=_strnicmp)
    target_link_libraries(linuxxdoom PRIVATE winmm)

    if(DOOM_ENABLE_DISCORD_RPC)
        target_compile_definitions(linuxxdoom PRIVATE DOOM_ENABLE_DISCORD_RPC=1)
    endif()

else()
    target_compile_definitions(linuxxdoom PRIVATE NORMALUNIX LINUX)

    if(DOOM_ENABLE_DISCORD_RPC)
        target_compile_definitions(linuxxdoom PRIVATE DOOM_ENABLE_DISCORD_RPC=1)
    endif()

    find_package(X11 REQUIRED)
    target_include_directories(linuxxdoom PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(linuxxdoom PRIVATE ${X11_X11_LIB} ${X11_Xext_LIB} m)
endif()
