cmake_minimum_required(VERSION 3.16)

project(doom_launcher LANGUAGES C)

add_executable(doom_launcher WIN32 launcher.c)

target_compile_definitions(doom_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)

target_include_directories(doom_launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(doom_launcher PRIVATE doom)
