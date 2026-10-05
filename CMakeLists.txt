cmake_minimum_required(VERSION 3.16)
project(PaintClone LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statische Laufzeitbibliothek: PaintClone.exe läuft ohne zusätzliche DLLs
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(PaintClone WIN32
    src/main.cpp
    src/canvas.cpp
    src/panels.cpp
    src/image.cpp
    src/shapes.cpp
    src/dialogs.cpp
    src/algo.cpp
    src/PaintClone.rc
)

target_compile_definitions(PaintClone PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(PaintClone PRIVATE
    gdiplus comctl32 comdlg32 gdi32 user32 shell32 shlwapi ole32 uuid advapi32)

if(MSVC)
    target_compile_options(PaintClone PRIVATE /utf-8 /W4 /permissive- /wd4100)
    # Das Manifest ist bereits in PaintClone.rc eingebunden
    target_link_options(PaintClone PRIVATE /MANIFEST:NO)
else()
    target_compile_options(PaintClone PRIVATE -municode -Wall -Wextra -Wno-unused-parameter
                                              -Wno-missing-field-initializers)
    target_link_options(PaintClone PRIVATE -municode -static)
endif()

# Tests der plattformunabhängigen Algorithmen
add_executable(test_algo tests/test_algo.cpp src/algo.cpp)
target_include_directories(test_algo PRIVATE src)
if(MSVC)
    target_compile_options(test_algo PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME algo COMMAND test_algo)
