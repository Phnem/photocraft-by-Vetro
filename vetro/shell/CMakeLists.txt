# PhotoCraft by Vetro -- the C++ shell (VetroView stack: Direct3D 11, Direct2D, DirectComposition)
# over the PhotoCraft engine, reached through crates/bridge (photocraft_bridge.dll).
# Build with vetro/build.ps1, which builds the bridge first (-Tests adds the CTest executables).
cmake_minimum_required(VERSION 3.20)
project(PhotoCraftVetro LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../target/release" CACHE PATH "Where cargo put photocraft_bridge.dll")
set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dist" CACHE PATH "Where the shell is assembled")
# The test executables are opt-in (build.ps1 -Tests), so a normal build leaves dist/ with the app only.
option(BUILD_TESTING "Build the CTest executables" OFF)

# Everything but the entry point: the app and the tests link the same shell code.
set(SHELL_SOURCES
  src/canvas.cpp
  src/brushes.cpp
  src/layer_controls.cpp
  src/retouch.cpp
  src/text_tools.cpp
  src/clipboard.cpp
  src/documents.cpp
  src/editors.cpp
  src/gfx.cpp
  src/i18n.cpp
  src/jobs.cpp
  src/tools.cpp
  src/parameters.cpp
  src/shapes.cpp
  src/crop.cpp
  src/sampler.cpp
  src/gradient.cpp
  src/transform.cpp
  src/widgets.cpp)
set(SHELL_LIBS d3d11 dxgi d2d1 dwrite dcomp dxguid dwmapi shell32 ole32 comdlg32 user32 gdi32 windowscodecs advapi32
  "${BRIDGE_DIR}/photocraft_bridge.dll.lib")

# One executable of the shell code: the options and the bridge DLL beside it are the same for all.
function(vetro_executable target)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX)
  target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W3 /EHsc /permissive- /Zc:__cplusplus> $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/O2>)
  target_link_libraries(${target} PRIVATE ${SHELL_LIBS})
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BRIDGE_DIR}/photocraft_bridge.dll" "${OUTPUT_DIR}/photocraft_bridge.dll")
endfunction()

add_executable(PhotoCraftVetro WIN32 src/app.cpp ${SHELL_SOURCES} app.rc)
vetro_executable(PhotoCraftVetro)

if(BUILD_TESTING)
  include(CTest)
  # target;source;ctest name;snapshot directory under the build tree
  foreach(suite "VetroInteractionTests;tests/milestone1.cpp;milestone1;qa"
                "VetroSystemTests;tests/milestone2.cpp;milestone2;qa-milestone2"
                "VetroParameterTests;tests/milestone3.cpp;milestone3;qa-milestone3"
                "VetroBrushLayerTests;tests/milestone4.cpp;milestone4;qa-milestone4"
                "VetroTextTests;tests/milestone5.cpp;milestone5;qa-milestone5")
    list(GET suite 0 target)
    list(GET suite 1 source)
    list(GET suite 2 name)
    list(GET suite 3 snapshots)
    add_executable(${target} ${source} ${SHELL_SOURCES})
    vetro_executable(${target})
    add_test(NAME ${name} COMMAND ${target} "${CMAKE_BINARY_DIR}/${snapshots}")
  endforeach()
endif()
