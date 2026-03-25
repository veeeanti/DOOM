cmake_minimum_required(VERSION 3.16)

project(linuxdoom VERSION 1.10.0 LANGUAGES C CXX)

option(DOOM_ENABLE_STEAMWORKS "Enable Steamworks transport integration" OFF)
option(DOOM_ENABLE_DISCORD_RPC "Enable Discord Rich Presence integration" ON)
set(STEAMWORKS_SDK_PATH "" CACHE PATH "Path to Steamworks SDK root")

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
        win32/steam_transport.cpp
        win32/i_video_win32.c
        win32/i_system_win32.c
        win32/i_sound_win32.c
        win32/ogg_temp.c
        win32/stb_vorbis.c
        win32/i_net_stub.c
        ${CMAKE_CURRENT_BINARY_DIR}/doom.rc
    )
else()
    set(DOOM_PLATFORM_SOURCES
        i_video.c
        i_system.c
        i_sound.c
        i_net.c
    )

    if(DOOM_ENABLE_STEAMWORKS)
        list(REMOVE_ITEM DOOM_PLATFORM_SOURCES i_net.c)
        list(APPEND DOOM_PLATFORM_SOURCES
            win32/steam_transport.cpp
            win32/i_net_stub.c
        )
    endif()
endif()

if(DOOM_ENABLE_DISCORD_RPC)
    list(APPEND DOOM_PLATFORM_SOURCES win32/discord_rpc_win32.c)
endif()

add_executable(linuxxdoom
    ${DOOM_CORE_SOURCES}
    ${DOOM_PLATFORM_SOURCES}
)

if(WIN32)
    add_executable(doom_launcher WIN32 win32/doom_launcher.c)
endif()

target_include_directories(linuxxdoom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    set_target_properties(linuxxdoom PROPERTIES OUTPUT_NAME doom)
    set_target_properties(linuxxdoom PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
    set_target_properties(doom_launcher PROPERTIES OUTPUT_NAME doom-launcher)

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
    target_compile_definitions(doom_launcher PRIVATE _CRT_SECURE_NO_WARNINGS _WIN32)

    if(DOOM_ENABLE_DISCORD_RPC)
        target_compile_definitions(linuxxdoom PRIVATE DOOM_ENABLE_DISCORD_RPC=1)
    endif()

    if(DOOM_ENABLE_STEAMWORKS)
        target_compile_definitions(linuxxdoom PRIVATE DOOM_ENABLE_STEAMWORKS=1)

        if(NOT STEAMWORKS_SDK_PATH)
            message(FATAL_ERROR "DOOM_ENABLE_STEAMWORKS=ON requires STEAMWORKS_SDK_PATH to be set")
        endif()

        target_include_directories(linuxxdoom PRIVATE ${STEAMWORKS_SDK_PATH}/public)

        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(STEAMWORKS_LIB ${STEAMWORKS_SDK_PATH}/redistributable_bin/win64/steam_api64.lib)
            set(STEAMWORKS_DLL ${STEAMWORKS_SDK_PATH}/redistributable_bin/win64/steam_api64.dll)
        else()
            set(STEAMWORKS_LIB ${STEAMWORKS_SDK_PATH}/redistributable_bin/steam_api.lib)
            set(STEAMWORKS_DLL ${STEAMWORKS_SDK_PATH}/redistributable_bin/steam_api.dll)
        endif()

        if(NOT EXISTS ${STEAMWORKS_LIB})
            message(FATAL_ERROR "Steamworks import library not found: ${STEAMWORKS_LIB}")
        endif()

        target_link_libraries(linuxxdoom PRIVATE ${STEAMWORKS_LIB})

        if(EXISTS ${STEAMWORKS_DLL})
            add_custom_command(TARGET linuxxdoom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${STEAMWORKS_DLL}
                    $<TARGET_FILE_DIR:linuxxdoom>
            )
        else()
            message(WARNING "Steamworks runtime DLL not found: ${STEAMWORKS_DLL}")
        endif()
    endif()
else()
    target_compile_definitions(linuxxdoom PRIVATE NORMALUNIX LINUX)

    if(DOOM_ENABLE_DISCORD_RPC)
        target_compile_definitions(linuxxdoom PRIVATE DOOM_ENABLE_DISCORD_RPC=1)
    endif()

    if(DOOM_ENABLE_STEAMWORKS)
        target_compile_definitions(linuxxdoom PRIVATE DOOM_ENABLE_STEAMWORKS=1)

        if(NOT STEAMWORKS_SDK_PATH)
            message(FATAL_ERROR "DOOM_ENABLE_STEAMWORKS=ON requires STEAMWORKS_SDK_PATH to be set")
        endif()

        target_include_directories(linuxxdoom PRIVATE ${STEAMWORKS_SDK_PATH}/public)

        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(STEAMWORKS_LIB ${STEAMWORKS_SDK_PATH}/redistributable_bin/linux64/libsteam_api.so)
        else()
            set(STEAMWORKS_LIB ${STEAMWORKS_SDK_PATH}/redistributable_bin/linux32/libsteam_api.so)
        endif()

        if(NOT EXISTS ${STEAMWORKS_LIB})
            message(FATAL_ERROR "Steamworks shared library not found: ${STEAMWORKS_LIB}")
        endif()

        target_link_libraries(linuxxdoom PRIVATE ${STEAMWORKS_LIB})
        target_link_options(linuxxdoom PRIVATE "-Wl,-rpath,$ORIGIN")

        add_custom_command(TARGET linuxxdoom POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${STEAMWORKS_LIB}
                $<TARGET_FILE_DIR:linuxxdoom>
        )
    endif()

    find_package(X11 REQUIRED)
    target_include_directories(linuxxdoom PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(linuxxdoom PRIVATE ${X11_X11_LIB} ${X11_Xext_LIB} m)
endif()
