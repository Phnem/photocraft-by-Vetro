# PhotoCraft by Vetro -- the C++ shell (VetroView stack: Direct3D 11, Direct2D, DirectComposition)
# over the PhotoCraft engine, reached through crates/bridge (photocraft_bridge.dll).
# Build with vetro/build.ps1, which builds the bridge first.
cmake_minimum_required(VERSION 3.20)
project(PhotoCraftVetro LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../target/release" CACHE PATH "Where cargo put photocraft_bridge.dll")
set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dist" CACHE PATH "Where the shell is assembled")

add_executable(PhotoCraftVetro WIN32
  src/app.cpp
  src/canvas.cpp
  src/editors.cpp
  src/gfx.cpp
  src/i18n.cpp
  src/jobs.cpp
  src/tools.cpp
  src/widgets.cpp
  app.rc)
target_compile_definitions(PhotoCraftVetro PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(PhotoCraftVetro PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W3 /EHsc /permissive- /Zc:__cplusplus> $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/O2>)
target_link_libraries(PhotoCraftVetro PRIVATE
  d3d11 dxgi d2d1 dwrite dcomp dxguid dwmapi shell32 ole32 comdlg32 user32 gdi32
  "${BRIDGE_DIR}/photocraft_bridge.dll.lib")
set_target_properties(PhotoCraftVetro PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
add_custom_command(TARGET PhotoCraftVetro POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BRIDGE_DIR}/photocraft_bridge.dll" "${OUTPUT_DIR}/photocraft_bridge.dll")
