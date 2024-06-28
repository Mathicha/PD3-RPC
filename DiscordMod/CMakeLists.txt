cmake_minimum_required(VERSION 3.18)

set(TARGET DiscordMod)
project(${TARGET})

add_library(${TARGET} SHARED "dllmain.cpp")
target_include_directories(${TARGET} PRIVATE .)

# UE4SS
target_link_libraries(${TARGET} PUBLIC UE4SS)

# discord-rpc
set(CLANG_FORMAT_CMD OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(discord-rpc)
include_directories(discord-rpc/include)
target_link_libraries(${TARGET} PUBLIC discord-rpc)

set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME "main")
