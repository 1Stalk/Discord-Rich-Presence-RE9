cmake_minimum_required(VERSION 3.20)
project(DiscordPresenceRE9 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output DLL
add_library(DiscordPresenceRE9 SHARED
    src/main.cpp
)

target_compile_definitions(DiscordPresenceRE9 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _UNICODE
    UNICODE
)

# Remove the "lib" prefix on Windows
set_target_properties(DiscordPresenceRE9 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "DiscordPresenceRE9"
)

# Optimized release build flags for MSVC
if(MSVC)
    target_compile_options(DiscordPresenceRE9 PRIVATE
        $<$<CONFIG:Release>:/O2 /GL /GS->
    )
    target_link_options(DiscordPresenceRE9 PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()

# Optional: install directory (can be overridden via command line)
set(GAME_PLUGINS_DIR
    ""
    CACHE PATH "REFramework plugins directory (e.g. C:/path/to/game/reframework/plugins)"
)

if(NOT GAME_PLUGINS_DIR STREQUAL "")
    install(TARGETS DiscordPresenceRE9
        RUNTIME DESTINATION "${GAME_PLUGINS_DIR}"
    )
    message(STATUS "Install target -> ${GAME_PLUGINS_DIR}")
endif()
