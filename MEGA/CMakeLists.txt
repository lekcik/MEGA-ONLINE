cmake_minimum_required(VERSION 3.20)
project(MEGA VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(mega
    src/main.cpp
    src/core/Platform.cpp
    src/net/NetSession.cpp
    src/render/GlApi.cpp
    src/render/Shader.cpp
    src/render/Framebuffer.cpp
    src/render/TextureAtlas.cpp
    src/render/WorldMesher.cpp
    src/render/BillboardRenderer.cpp
    src/render/RobotRenderer.cpp
    src/render/HudRenderer.cpp
    src/world/WorldGrid.cpp
    src/world/LightBaker.cpp
    src/world/gen/MegaGenerator.cpp
    src/game/Player.cpp
    src/game/GameWorld.cpp
    src/game/Inventory3D.cpp
    src/game/LockpickMinigame.cpp
    src/audio/AudioEngine.cpp
)

target_include_directories(mega PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mega PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(mega PRIVATE opengl32 ws2_32 winmm gdi32 user32)
    if(MINGW)
        target_link_options(mega PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
elseif(UNIX)
    target_link_libraries(mega PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MSVC)
    target_compile_options(mega PRIVATE /W4 /permissive-)
else()
    target_compile_options(mega PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/loot_tables.cfg")
    add_custom_command(TARGET mega POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/loot_tables.cfg"
            "$<TARGET_FILE_DIR:mega>/loot_tables.cfg"
    )
endif()

# Optional GoogleTest for generator, physics, multiplayer & networking unit tests
find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()
    add_executable(mega_tests
        tests/CoreTests.cpp
        src/core/Platform.cpp
        src/net/NetSession.cpp
        src/world/WorldGrid.cpp
        src/world/LightBaker.cpp
        src/world/gen/MegaGenerator.cpp
        src/game/Player.cpp
        src/game/GameWorld.cpp
        src/game/Inventory3D.cpp
        src/game/LockpickMinigame.cpp
        src/audio/AudioEngine.cpp
    )
    target_include_directories(mega_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mega_tests PRIVATE GTest::gtest_main Threads::Threads)
    if(WIN32)
        target_link_libraries(mega_tests PRIVATE opengl32 ws2_32 winmm gdi32 user32)
    elseif(UNIX)
        target_link_libraries(mega_tests PRIVATE ${CMAKE_DL_LIBS})
    endif()
    include(GoogleTest)
    gtest_discover_tests(mega_tests)
endif()
