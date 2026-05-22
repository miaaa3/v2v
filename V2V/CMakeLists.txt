cmake_minimum_required(VERSION 3.20)
project(V2V
    VERSION 0.1.0
    DESCRIPTION "Vehicle-to-Vehicle simulator with real-time visualization"
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# C++ standard / global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Warnings
if(MSVC)
    add_compile_options(/W4 /permissive- /wd4201)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Dependencies via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ----- GLFW ----------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

# ----- GLM (header-only) ---------------------------------------------------
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

# ----- GLAD2 (OpenGL loader generated at configure time) -------------------
# Requires Python 3 on PATH (used by the glad generator).
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake
)

# ----- Dear ImGui ----------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)

# ----- stb (header-only image decoder) -------------------------------------
# stb has no tagged releases; pinning to master is the standard practice.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# ----- pugixml (small, fast XML parser for OSM data) -----------------------
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw glm glad imgui stb pugixml)

# ----- libcurl (system) ----------------------------------------------------
# Used by TileLoader to fetch OSM raster tiles. On macOS curl ships with the
# system; on Linux install libcurl-dev; on Windows use vcpkg.
find_package(CURL REQUIRED)

# After glad's CMake module is loaded, generate a static GL 3.3 Core lib
glad_add_library(glad_gl_core_33 STATIC API gl:core=3.3)

# Build ImGui ourselves (with GLFW + OpenGL3 backends)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

# ---------------------------------------------------------------------------
# V2V executable
# ---------------------------------------------------------------------------
set(V2V_SOURCES
    src/main.cpp
    src/graphics/Window.cpp
    src/graphics/Shader.cpp
    src/graphics/Renderer.cpp
    src/graphics/Camera2D.cpp
    src/map/MapProjection.cpp
    src/map/TileCache.cpp
    src/map/TileLoader.cpp
    src/map/TileRenderer.cpp
    src/graph/RoadGraph.cpp
    src/graph/OSMParser.cpp
    src/graph/GraphRenderer.cpp
    src/simulation/PathFinder.cpp
    src/simulation/SimulationEngine.cpp
    src/simulation/TrafficLightSystem.cpp
    src/simulation/VehicleRenderer.cpp
    src/simulation/VehicleType.cpp
    src/simulation/AccidentSystem.cpp
    src/simulation/DensityHeatmap.cpp
    src/v2v/Broadcast.cpp
    src/v2v/BroadcastRenderer.cpp
    src/v2v/V2VNetwork.cpp
    src/v2v/LinkRenderer.cpp
    src/ui/ImGuiLayer.cpp
    src/ui/Panels.cpp
    src/utils/Profiler.cpp
    src/utils/RainRenderer.cpp
    src/utils/SpatialGrid.cpp
    src/vendor/stb_image_impl.cpp
)

add_executable(V2V ${V2V_SOURCES})

target_include_directories(V2V PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${stb_SOURCE_DIR}
)

target_link_libraries(V2V PRIVATE
    glfw
    glm::glm
    glad_gl_core_33
    imgui
    CURL::libcurl
    pugixml::pugixml
)

# Make the shader path discoverable at runtime in two ways:
#  - copy shaders next to the binary (for distribution)
#  - expose the source-tree shader dir as a compile definition (for dev runs)
add_custom_command(TARGET V2V POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:V2V>/shaders
)

target_compile_definitions(V2V PRIVATE
    V2V_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
)

# Platform-specific link flags
if(APPLE)
    target_link_libraries(V2V PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
elseif(UNIX)
    target_link_libraries(V2V PRIVATE ${CMAKE_DL_LIBS})
endif()

message(STATUS "V2V configured. Build with: cmake --build ${CMAKE_BINARY_DIR}")
