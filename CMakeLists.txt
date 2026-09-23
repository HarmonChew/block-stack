cmake_minimum_required(VERSION 3.20)
project(project_name VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(PROJECT_BUILD_APP "Build the SDL3 desktop client" ON)
option(PROJECT_BUILD_PYTHON "Build the native library used by the Python package" ON)
option(PROJECT_BUILD_TESTS "Build core tests" ON)

add_library(blocks_core STATIC
    core/src/game.cpp
    core/src/pieces.cpp
    core/src/rng.cpp
    core/src/rules.cpp
    core/src/replay.cpp
)
set_target_properties(blocks_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(blocks_core PUBLIC core/include)
target_compile_features(blocks_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(blocks_core PRIVATE /W4)
else()
    target_compile_options(blocks_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PROJECT_BUILD_PYTHON)
    add_library(blocks_native SHARED core/src/c_api.cpp)
    target_link_libraries(blocks_native PRIVATE blocks_core)
    target_compile_definitions(blocks_native PRIVATE BLOCKS_C_API_EXPORTS)
    set_target_properties(blocks_native PROPERTIES OUTPUT_NAME blocks_native)
endif()

foreach(tool headless replay benchmark rng info verify)
    add_executable(project_name_${tool} tools/cli.cpp)
    target_link_libraries(project_name_${tool} PRIVATE blocks_core)
    target_compile_definitions(project_name_${tool} PRIVATE BLOCKS_TOOL="${tool}")
endforeach()

if(PROJECT_BUILD_APP)
    find_package(SDL3 QUIET CONFIG)
    if(NOT SDL3_FOUND AND PROJECT_SDL3_INCLUDE_DIR)
        find_library(PROJECT_SDL3_LIBRARY NAMES SDL3 SDL3.so.0 libSDL3.so.0)
        if(NOT PROJECT_SDL3_LIBRARY)
            message(FATAL_ERROR "SDL3 runtime library not found")
        endif()
        add_library(SDL3::SDL3 SHARED IMPORTED)
        set_target_properties(SDL3::SDL3 PROPERTIES
            IMPORTED_LOCATION "${PROJECT_SDL3_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SDL3_INCLUDE_DIR}")
    elseif(NOT SDL3_FOUND)
        message(FATAL_ERROR "SDL3 development files are required for the desktop client. Install libsdl3-dev or set PROJECT_BUILD_APP=OFF.")
    endif()
    add_executable(project_name app/src/main.cpp)
    target_link_libraries(project_name PRIVATE blocks_core SDL3::SDL3)
endif()

if(PROJECT_BUILD_TESTS)
    enable_testing()
    add_executable(blocks_tests tests/core.cpp tests/rng_rules.cpp tests/replay.cpp)
    target_link_libraries(blocks_tests PRIVATE blocks_core)
    add_test(NAME blocks_core COMMAND blocks_tests)
    if(PROJECT_BUILD_APP AND UNIX AND NOT APPLE)
        add_test(NAME blocks_app_keyboard COMMAND project_name --keyboard-smoke)
        set_tests_properties(blocks_app_keyboard PROPERTIES ENVIRONMENT
            "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    endif()
    if(PROJECT_BUILD_PYTHON)
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME blocks_python
                COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests/python -v)
            set_tests_properties(blocks_python PROPERTIES ENVIRONMENT
                "PYTHONPATH=${CMAKE_SOURCE_DIR}/python;BLOCKS_NATIVE_LIB=$<TARGET_FILE:blocks_native>;PYTHONDONTWRITEBYTECODE=1")
        endif()
    endif()
endif()
