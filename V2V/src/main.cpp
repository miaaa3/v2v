// =============================================================================
// V2V Simulator — full demo (Étapes 1–8)
//
//   - Étape 3: OSM raster map (tile.openstreetmap.org / CartoDB voyager).
//   - Étape 4: parsed road graph rendered as thick triangles.
//   - Étape 5: A* path-following vehicles moving on the graph.
//   - Étape 6: Profiler + SpatialGrid + instanced vehicle rendering.
//   - Étape 7: V2V network (proximity links) with strength-coloured edges.
//   - Étape 8: Dear ImGui control / debug panels.
//
//     ./V2V data/mulhouse.osm           # default 1000 vehicles
//     ./V2V data/mulhouse.osm 2000      # explicit count
//
// Keyboard / mouse — these still work when the cursor is *not* over an ImGui
// panel; pan/zoom are suppressed when ImGui wants the mouse.
//   Right-drag : pan      Wheel : zoom        R : reset camera
//   G / M / V / L : toggle graph / map / vehicles / V2V links
//   SPACE : pause          + / - : time scale         0 : 1x
//   F1 : stdout overlay    ESC : quit
// =============================================================================

#include "graphics/Camera2D.h"
#include "graphics/Renderer.h"
#include "graphics/Window.h"
#include "graph/GraphRenderer.h"
#include "graph/OSMParser.h"
#include "graph/RoadGraph.h"
#include "map/MapProjection.h"
#include "map/TileCache.h"
#include "map/TileLoader.h"
#include "map/TileRenderer.h"
#include "simulation/AccidentSystem.h"
#include "simulation/DensityHeatmap.h"
#include "simulation/SimulationEngine.h"
#include "simulation/VehicleRenderer.h"
#include "simulation/VehicleType.h"
#include "ui/ImGuiLayer.h"
#include "ui/Panels.h"
#include "utils/Log.h"
#include "utils/Profiler.h"
#include "utils/RainRenderer.h"
#include "utils/SpatialGrid.h"
#include "utils/Weather.h"
#include "v2v/Broadcast.h"
#include "v2v/BroadcastRenderer.h"
#include "v2v/LinkRenderer.h"
#include "v2v/V2VNetwork.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

// Channel used by the mini-map panel to ask main() to jump the camera. The
// panel doesn't have direct access to the Camera2D instance, so it sets this
// shared state via a forward-declared free function (see Panels.cpp).
namespace {
glm::vec2 g_minimapJumpTarget{0.0f, 0.0f};
bool      g_minimapJumpReady = false;
} // namespace
void minimapJumpRequest(glm::vec2 worldPos)
{
    g_minimapJumpTarget = worldPos;
    g_minimapJumpReady  = true;
}

namespace {

#ifdef V2V_SHADER_DIR
constexpr const char* kShaderDir = V2V_SHADER_DIR;
#else
constexpr const char* kShaderDir = "shaders";
#endif

constexpr double kDefaultLatDeg = 33.5731; // Casablanca
constexpr double kDefaultLonDeg = -7.5898;
constexpr int    kDefaultZoom   = 13;
constexpr std::size_t kDefaultVehicleCount = 1000;

struct CameraView {
    glm::vec2 position;
    float     zoom;
};

CameraView defaultView()
{
    CameraView v;
    const auto c = v2v::map::MapProjection::latLonToWorld(kDefaultLatDeg, kDefaultLonDeg);
    v.position = glm::vec2(static_cast<float>(c.x), static_cast<float>(c.y));
    v.zoom     = static_cast<float>(v2v::map::MapProjection::TILE_PIXELS
                                    / v2v::map::MapProjection::tileSizeMeters(kDefaultZoom));
    return v;
}

CameraView viewFromGraphBounds(const v2v::graph::RoadGraph& graph, int vpW, int vpH)
{
    if (graph.nodeCount() == 0) return defaultView();
    const glm::dvec4 b = graph.worldBounds();
    const double cx = 0.5 * (b.x + b.z);
    const double cy = 0.5 * (b.y + b.w);
    const double w  = std::max(1.0, b.z - b.x);
    const double h  = std::max(1.0, b.w - b.y);
    const double z  = 0.9 * std::min(static_cast<double>(vpW) / w,
                                     static_cast<double>(vpH) / h);
    CameraView v;
    v.position = glm::vec2(static_cast<float>(cx), static_cast<float>(cy));
    v.zoom     = static_cast<float>(std::max(z, 1e-4));
    return v;
}

// Used by the ImGui "Reset camera" button.
struct CameraResetCtx {
    v2v::graphics::Camera2D*       camera;
    const v2v::graph::RoadGraph*   graph;
    bool*                          graphLoaded;
    int                            width;
    int                            height;
};

void resetCameraThunk(void* userdata)
{
    auto* ctx = static_cast<CameraResetCtx*>(userdata);
    const CameraView v = (*ctx->graphLoaded)
        ? viewFromGraphBounds(*ctx->graph, ctx->width, ctx->height)
        : defaultView();
    ctx->camera->setPosition(v.position);
    ctx->camera->setZoom(v.zoom);
}

} // namespace

int main(int argc, char** argv) try
{
    using namespace v2v;
    using namespace v2v::graphics;
    using namespace v2v::map;
    using namespace v2v::graph;
    using namespace v2v::simulation;
    using namespace v2v::vnet;
    using namespace v2v::ui;

    constexpr int kInitialWidth  = 1280;
    constexpr int kInitialHeight = 720;

    Window window(kInitialWidth, kInitialHeight,
                  "V2V Simulator — full (steps 1–8)");

    // ---- Renderers ------------------------------------------------------
    Renderer primitives;
    if (!primitives.init(kShaderDir)) return 1;
    primitives.setViewport(0, 0, window.width(), window.height());

    TileRenderer       tilesR;       if (!tilesR.init     (kShaderDir)) return 1;
    GraphRenderer      graphR;       if (!graphR.init     (kShaderDir)) return 1;
    VehicleRenderer    vehiclesR;    if (!vehiclesR.init  (kShaderDir)) return 1;
    LinkRenderer       linksR;       if (!linksR.init     (kShaderDir)) return 1;
    BroadcastRenderer  bcastR;       if (!bcastR.init     (kShaderDir)) return 1;
    v2v::utils::RainRenderer rainR;  if (!rainR.init      (kShaderDir)) return 1;

    TileCache  tileCache(512);
    TileLoader tileLoader(4);

    // ---- UI -------------------------------------------------------------
    ImGuiLayer imgui;
    if (!imgui.init(window)) {
        V2V_LOG_ERROR("ImGui initialization failed.");
        return 1;
    }

    // ---- Optional OSM graph + simulation --------------------------------
    RoadGraph    graph;
    bool         graphLoaded = false;
    std::string  osmPath;
    std::size_t  vehicleCount = kDefaultVehicleCount;

    if (argc > 1) {
        osmPath = argv[1];
        V2V_LOG_INFO("Loading OSM data from: %s", osmPath.c_str());
        const auto st = OSMParser::parseFile(osmPath, graph);
        if (!st.ok()) {
            V2V_LOG_ERROR("OSM parse failed: %s", st.error.c_str());
        } else if (graph.edgeCount() == 0) {
            V2V_LOG_WARN("OSM file parsed but contains no routable highways.");
        } else {
            graphR.buildGeometry(graph);
            graphLoaded = true;
        }
    } else {
        V2V_LOG_INFO("No OSM file provided. Run with: ./V2V path/to/data.osm [N]");
    }
    if (argc > 2) {
        const long n = std::strtol(argv[2], nullptr, 10);
        if (n > 0) vehicleCount = static_cast<std::size_t>(n);
    }

    SimulationEngine sim(graph);
    if (graphLoaded) sim.spawn(vehicleCount);

    utils::SpatialGrid spatial;
    if (graphLoaded) {
        const glm::dvec4 b = graph.worldBounds();
        const float margin = 1000.0f;
        spatial.rebuild(
            { static_cast<float>(b.x) - margin, static_cast<float>(b.y) - margin },
            { static_cast<float>(b.z) + margin, static_cast<float>(b.w) + margin },
            500.0f);
        V2V_LOG_INFO("SpatialGrid: %dx%d cells (%.1f m each)",
                     spatial.cols(), spatial.rows(), spatial.cellSize());
    }

    V2VNetwork      v2vNet;
    v2vNet.setMaxRange(350.0f);

    BroadcastSystem broadcastSys;
    AccidentSystem  accidentSys;

    DensityHeatmap  heatmap;
    if (graphLoaded) {
        const glm::dvec4 b = graph.worldBounds();
        const float margin = 1000.0f;
        heatmap.rebuild(
            { static_cast<float>(b.x) - margin, static_cast<float>(b.y) - margin },
            { static_cast<float>(b.z) + margin, static_cast<float>(b.w) + margin },
            150.0f);
    }

    // ---- Camera ---------------------------------------------------------
    Camera2D camera(window.width(), window.height());
    {
        const CameraView v = graphLoaded
            ? viewFromGraphBounds(graph, window.width(), window.height())
            : defaultView();
        camera.setPosition(v.position);
        camera.setZoom(v.zoom);
    }

    CameraResetCtx camResetCtx{ &camera, &graph, &graphLoaded,
                                window.width(), window.height() };

    // Callback context for the "Broadcast V2V signal" button.
    struct BroadcastCtx {
        BroadcastSystem*  sys;
        SimulationEngine* sim;
        const Window*     win;
    };
    BroadcastCtx broadcastCtx{ &broadcastSys, &sim, &window };
    auto broadcastThunk = [](void* ud, std::uint32_t srcId) {
        auto* ctx = static_cast<BroadcastCtx*>(ud);
        ctx->sys->emit(srcId, ctx->sim->pool(), ctx->win->time());
    };

    // ---- Input ---------------------------------------------------------
    glm::vec2 mousePos    { 0.0f, 0.0f };
    glm::vec2 lastMouse   { 0.0f, 0.0f };
    glm::vec2 mouseDownPos{ 0.0f, 0.0f };
    bool      panning      = false;     // currently dragging (either button)
    bool      leftDown     = false;
    bool      leftDragged  = false;     // moved more than threshold while held
    bool      showGraph    = true;
    bool      showTiles    = true;
    bool      showVehicles  = true;
    bool      showLinks     = true;
    bool      showHeatmap   = false;
    bool      showAccidents = true;
    bool      showOverlay   = true;
    int       selectedVehicleId    = -1;
    float     vehicleSizeMultiplier = 1.0f;  // user-controllable in Controls panel
    bool      followSelected       = false;  // cinematic camera follow
    bool      droneMode            = false;  // auto-cycling cinematic
    bool      camera3D             = false;  // pseudo-3D tilted camera (Step 1)
    bool      showVehicleLights    = true;   // brake + emergency lights overlay
    bool      showTrails           = true;   // cinematic ghost trails
    float     timeOfDay            = 0.50f;  // 0 = midnight, 0.5 = noon, 1 = midnight

    // Weather + V2V baseline (so rain/fog can scale the configured range).
    v2v::utils::Weather weather;
    constexpr float kBaseV2VRange  = 350.0f;

    constexpr float kDragThresholdPx = 5.0f;

    // Pick the closest vehicle within `worldRadius` of `worldPos` via the
    // SpatialGrid. Returns its id, or -1 if no candidate.
    auto pickClosestVehicle = [&](glm::vec2 worldPos, float worldRadius) -> int {
        if (sim.pool().empty()) return -1;
        static thread_local std::vector<std::uint32_t> cand;
        cand.clear();
        spatial.queryRadius(worldPos, worldRadius, cand);
        int   best  = -1;
        float bestD = worldRadius * worldRadius;
        for (auto id : cand) {
            if (id >= sim.pool().size()) continue;
            const auto p = sim.pool()[id].position;
            const float d = (p.x - worldPos.x) * (p.x - worldPos.x)
                          + (p.y - worldPos.y) * (p.y - worldPos.y);
            if (d < bestD) { bestD = d; best = static_cast<int>(id); }
        }
        return best;
    };

    window.setCursorPosCallback([&](double x, double y) {
        // ImGui receives RAW window coordinates (what GLFW gives us). This is
        // what its hover/hit-testing expects.
        ImGui_ImplGlfw_CursorPosCallback(window.handle(), x, y);

        // For our camera math we need FRAMEBUFFER coordinates (the viewport
        // is framebuffer-sized). On Retina / HiDPI the two differ.
        int winW = 0, winH = 0, fbW = 0, fbH = 0;
        glfwGetWindowSize     (window.handle(), &winW, &winH);
        glfwGetFramebufferSize(window.handle(), &fbW,  &fbH);
        const float sx = (winW > 0) ? static_cast<float>(fbW) / static_cast<float>(winW) : 1.0f;
        const float sy = (winH > 0) ? static_cast<float>(fbH) / static_cast<float>(winH) : 1.0f;

        lastMouse = mousePos;
        mousePos  = { static_cast<float>(x) * sx, static_cast<float>(y) * sy };
        if (imgui.wantCaptureMouse()) return;

        if (leftDown) {
            const float dx = mousePos.x - mouseDownPos.x;
            const float dy = mousePos.y - mouseDownPos.y;
            if (!leftDragged && (std::abs(dx) > kDragThresholdPx ||
                                 std::abs(dy) > kDragThresholdPx))
            {
                leftDragged = true;
                panning     = true;
            }
        }
        if (panning) camera.pan(mousePos - lastMouse);
    });

    window.setMouseButtonCallback([&](int button, int action, int mods) {
        ImGui_ImplGlfw_MouseButtonCallback(window.handle(), button, action, mods);
        if (imgui.wantCaptureMouse()) { panning = false; leftDown = false; return; }

        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                leftDown     = true;
                leftDragged  = false;
                mouseDownPos = mousePos;
            } else {
                const bool wasDrag = leftDragged;
                leftDown     = false;
                leftDragged  = false;
                panning      = false;
                if (!wasDrag) {
                    // Treat as click — try to pick a vehicle. The pick radius
                    // is the bigger of 20 screen pixels and 12 world meters,
                    // so it works at both low and high zoom.
                    const float radiusWorld =
                        std::max(12.0f, 20.0f / std::max(camera.zoom(), 1e-4f));
                    const glm::vec2 w = camera.screenToWorld(mousePos);
                    const int hit = pickClosestVehicle(w, radiusWorld);
                    selectedVehicleId = hit;   // -1 deselects
                }
            }
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT ||
                   button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            panning = (action == GLFW_PRESS);
        }
    });
    window.setScrollCallback([&](double xOff, double yOff) {
        ImGui_ImplGlfw_ScrollCallback(window.handle(), xOff, yOff);
        if (imgui.wantCaptureMouse()) return;
        const float factor = (yOff > 0.0) ? 1.15f : (1.0f / 1.15f);
        camera.zoomAt(factor, mousePos);
    });
    window.setKeyCallback([&](int key, int sc, int action, int mods) {
        ImGui_ImplGlfw_KeyCallback(window.handle(), key, sc, action, mods);
        if (imgui.wantCaptureKeyboard()) return;
        if (action != GLFW_PRESS) return;
        switch (key) {
            case GLFW_KEY_ESCAPE: window.requestClose(); break;
            case GLFW_KEY_R:      resetCameraThunk(&camResetCtx); break;
            case GLFW_KEY_G:     showGraph    = !showGraph;    break;
            case GLFW_KEY_M:     showTiles    = !showTiles;    break;
            case GLFW_KEY_V:     showVehicles = !showVehicles; break;
            case GLFW_KEY_L:     showLinks    = !showLinks;    break;
            case GLFW_KEY_H:     showHeatmap  = !showHeatmap;
                                 heatmap.setEnabled(showHeatmap); break;
            case GLFW_KEY_X:     accidentSys.forceTrigger();   break;
            case GLFW_KEY_F1:    showOverlay  = !showOverlay;  break;
            case GLFW_KEY_SPACE: sim.togglePaused();           break;
            case GLFW_KEY_EQUAL:
            case GLFW_KEY_KP_ADD:
                sim.setTimeScale(std::min(16.0f, sim.timeScale() * 2.0f));
                break;
            case GLFW_KEY_MINUS:
            case GLFW_KEY_KP_SUBTRACT:
                sim.setTimeScale(std::max(0.0625f, sim.timeScale() * 0.5f));
                break;
            case GLFW_KEY_0:     sim.setTimeScale(1.0f);       break;
            case GLFW_KEY_T:     camera3D = !camera3D;         break;
            default: break;
        }
    });
    window.setFramebufferSizeCallback([&](int w, int h) {
        primitives.setViewport(0, 0, w, h);
        camera.setViewport(w, h);
        camResetCtx.width  = w;
        camResetCtx.height = h;
    });

    V2V_LOG_INFO("Full V2V demo running. UI panels on the left; close them with the [x] if you prefer.");

    // ---- Main loop -----------------------------------------------------
    double lastTime  = window.time();
    double fpsAccum  = 0.0;
    int    fpsFrames = 0;
    double lastFrameMs = 0.0;
    double lastFps     = 0.0;

    while (!window.shouldClose()) {
        const double now = window.time();
        const float  dt  = static_cast<float>(now - lastTime);
        lastTime = now;

        sim.setNow(now);
        {
            V2V_PROF("sim/step");
            sim.setNow(now);
            sim.step(std::min(dt, 0.1f));
        }

        // ---- Cinematic camera ---------------------------------------------
        if (followSelected && selectedVehicleId >= 0 &&
            selectedVehicleId < static_cast<int>(sim.pool().size()))
        {
            // Set target to the selected vehicle's current position; the
            // exponential smoothing inside Camera2D::tick does the easing.
            const auto& fv = sim.pool()[static_cast<std::size_t>(selectedVehicleId)];
            camera.setTargetPosition(fv.position);
        }
        else if (droneMode && graphLoaded && !sim.pool().empty()) {
            // Auto-cinematic: every ~6 s, pick a fresh point of interest and
            // glide there. Priority: active accident → recent broadcast →
            // densest SpatialGrid cell → random vehicle.
            static double s_droneNextSwitch = 0.0;
            static glm::vec2 s_droneTarget{0.0f, 0.0f};
            static std::mt19937 s_droneRng{0xD0DEC0DEu};

            if (now >= s_droneNextSwitch) {
                s_droneNextSwitch = now + 6.0;
                bool chose = false;

                if (!accidentSys.active().empty()) {
                    std::uniform_int_distribution<std::size_t>
                        d(0, accidentSys.active().size() - 1);
                    s_droneTarget = accidentSys.active()[d(s_droneRng)].position;
                    chose = true;
                }
                if (!chose && broadcastSys.count() > 0) {
                    std::uniform_int_distribution<std::size_t>
                        d(0, broadcastSys.active().size() - 1);
                    s_droneTarget = broadcastSys.active()[d(s_droneRng)].origin;
                    chose = true;
                }
                if (!chose) {
                    // Pick a random in-range vehicle.
                    std::uniform_int_distribution<std::size_t>
                        d(0, sim.pool().size() - 1);
                    s_droneTarget = sim.pool()[d(s_droneRng)].position;
                }
            }
            camera.setTargetPosition(s_droneTarget);
        }
        camera.setTilted(camera3D);   // pseudo-3D mode follows the UI / 'T' toggle
        camera.tick(std::min(dt, 0.1f));
        if (graphLoaded) {
            V2V_PROF("accidents/tick");
            accidentSys.tick(now, std::min(dt, 0.1f), sim, &broadcastSys);
        }
        if (graphLoaded) {
            V2V_PROF("spatial/rebuild");
            spatial.clear();
            for (const auto& v : sim.pool()) spatial.insert(v.id, v.position);
        }
        if (graphLoaded) {
            // Apply weather to the V2V max range so rain/fog actually shrinks
            // the comms network (visible in the link count).
            v2vNet.setMaxRange(kBaseV2VRange * weather.v2vRangeFactor());
            V2V_PROF("v2v/update");
            v2vNet.update(sim.pool(), spatial);
        }

        // Rain particles update (cheap, capped).
        if (weather.mode == utils::WeatherMode::Rain) {
            V2V_PROF("weather/update");
            rainR.update(camera, std::min(dt, 0.1f));
        }
        if (graphLoaded) {
            V2V_PROF("v2v/broadcast");
            broadcastSys.tick(now, sim.pool());
        }

        // Emergency yielding: mark any civilian close-to + ahead of an
        // emergency vehicle to slow down for 1.5 s. Uses the SpatialGrid for
        // an O(N_emergency * cell_density) lookup.
        if (graphLoaded) {
            V2V_PROF("emergency/yield");
            constexpr float kYieldRadius   = 45.0f; // m
            constexpr double kYieldHold    = 1.5;   // seconds
            static thread_local std::vector<std::uint32_t> cand;
            for (auto& v : sim.pool()) {
                if (!simulation::typeInfo(v.type).emergency) continue;
                cand.clear();
                spatial.queryRadius(v.position, kYieldRadius, cand);
                for (auto id : cand) {
                    if (id == v.id || id >= sim.pool().size()) continue;
                    auto& other = sim.pool().vehicles()[id];
                    if (simulation::typeInfo(other.type).emergency) continue;
                    other.yieldingUntil = now + kYieldHold;
                }
            }
        }

        if (graphLoaded) {
            V2V_PROF("heatmap/update");
            heatmap.setEnabled(showHeatmap);
            heatmap.update(std::min(dt, 0.1f), sim.pool());
        }
        if (graphLoaded) {
            V2V_PROF("sim/accidents");
            accidentSys.tick(now, dt, sim, &broadcastSys);
        }
        if (graphLoaded && showHeatmap) {
            V2V_PROF("sim/density");
            heatmap.update(dt, sim.pool());
        }

        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 1.0) {
            lastFrameMs = 1000.0 * fpsAccum / fpsFrames;
            lastFps     = static_cast<double>(fpsFrames) / fpsAccum;
            if (showOverlay) {
                const glm::dvec2 ll = MapProjection::worldToLatLon(
                    glm::dvec2(camera.position().x, camera.position().y));
                V2V_LOG_INFO(
                    "FPS=%.0f  frame=%.2fms  veh=%zu  links=%zu  tiles=%zu  ts=%.2fx  "
                    "lat=%.4f lon=%.4f",
                    lastFps, lastFrameMs,
                    sim.pool().size(), v2vNet.linkCount(),
                    tilesR.lastFrameVisibleTileCount(),
                    sim.timeScale(), ll.x, ll.y);
            }
            fpsAccum  = 0.0;
            fpsFrames = 0;
        }

        window.pollEvents();

        // ---- World render --------------------------------------------
        // Day/night blend: timeOfDay in [0,1], 0/1 = midnight, 0.5 = noon.
        // We compute a brightness 0..1 (peak at noon, dark at night).
        const float tod = std::clamp(timeOfDay, 0.0f, 1.0f);
        const float dayCurve = 0.5f - 0.5f *
                               std::cos(tod * 6.28318530718f);   // 0..1..0
        {
            V2V_PROF("render/clear");
            primitives.beginFrame(camera);
            // Night = deep navy, noon = lighter background.
            primitives.clear(0.03f + 0.04f * dayCurve,
                             0.04f + 0.05f * dayCurve,
                             0.07f + 0.06f * dayCurve, 1.0f);
            primitives.endFrame();
        }
        if (showTiles)                    { V2V_PROF("render/tiles");   tilesR.render(camera, tileCache, tileLoader); }
        if (showHeatmap  && graphLoaded)  { V2V_PROF("render/heatmap"); heatmap.render(primitives, camera); }
        if (showGraph    && graphLoaded)  { V2V_PROF("render/graph");   graphR.render(camera); }

        // Rain layer drawn on top of the map but under the vehicles, so cars
        // appear in front of the streaks (more depth).
        if (weather.mode == utils::WeatherMode::Rain && weather.intensity > 0.0f) {
            V2V_PROF("render/rain");
            rainR.render(camera, weather.intensity);
        }

        // Fog: a thin grey-white wash covering the whole viewport. Cheap.
        if (weather.mode == utils::WeatherMode::Fog && weather.intensity > 0.0f) {
            V2V_PROF("render/fog");
            primitives.beginFrame(camera);
            const float ppm   = std::max(camera.zoom(), 1e-6f);
            const float halfW = static_cast<float>(camera.viewportWidth())  * 0.5f / ppm;
            const float halfH = static_cast<float>(camera.viewportHeight()) * 0.5f / ppm;
            primitives.drawQuad(camera.position(),
                                glm::vec2(halfW * 2.2f, halfH * 2.2f),
                                glm::vec4(0.78f, 0.82f, 0.88f, 0.40f * weather.intensity));
            primitives.endFrame();
        }

        // Adapt vehicle world-size to stay visible at any zoom — and on
        // Retina / HiDPI, target *window* pixels, not framebuffer pixels.
        // The user can scale the result further via `vehicleSizeMultiplier`.
        if (showVehicles && graphLoaded) {
            // Min/Max sizes expressed in WINDOW (logical) pixels.
            constexpr float kMinPxWindow    = 14.0f;
            constexpr float kMaxPxWindow    = 100.0f;
            constexpr float kReferenceLenM  = 4.5f;   // a car's length

            // Convert to framebuffer pixels (camera.zoom() is in those units).
            int winW = 0, fbW = 0, winH = 0, fbH = 0;
            glfwGetWindowSize     (window.handle(), &winW, &winH);
            glfwGetFramebufferSize(window.handle(), &fbW,  &fbH);
            const float contentScale = (winW > 0)
                ? static_cast<float>(fbW) / static_cast<float>(winW)
                : 1.0f;
            const float kMinPxFb = kMinPxWindow * contentScale;
            const float kMaxPxFb = kMaxPxWindow * contentScale;

            const float naturalPx = kReferenceLenM * std::max(camera.zoom(), 1e-6f);
            float scale = 1.0f;
            if      (naturalPx < kMinPxFb) scale = kMinPxFb / naturalPx;
            else if (naturalPx > kMaxPxFb) scale = kMaxPxFb / naturalPx;
            vehiclesR.setGlobalScale(scale * std::max(0.5f, vehicleSizeMultiplier));

            V2V_PROF("render/vehicles");
            // Step 2: pass the clock (flashers / glow pulse) and the night
            // factor (1 - dayCurve) so headlights come on after dark.
            vehiclesR.render(camera, sim.pool(),
                             static_cast<float>(now), 1.0f - dayCurve);
        }

        if (showLinks && graphLoaded)  { V2V_PROF("render/links"); linksR.render(camera, v2vNet, sim.pool()); }

        // ---- Vehicle lights (brake + emergency) --------------------------
        // Only enabled when zoomed in enough to actually see the dots, and
        // frustum-culled so we never iterate more than the visible vehicles.
        if (showVehicleLights && graphLoaded && camera.zoom() > 0.5f) {
            V2V_PROF("render/vehicle-lights");
            const float ppm   = std::max(camera.zoom(), 1e-6f);
            const float halfW = static_cast<float>(camera.viewportWidth())  * 0.5f / ppm;
            const float halfH = static_cast<float>(camera.viewportHeight()) * 0.5f / ppm;
            const glm::vec2 cam = camera.position();
            const float vL = cam.x - halfW, vR = cam.x + halfW;
            const float vB = cam.y - halfH, vT = cam.y + halfH;

            // Dot size scales with zoom (always a few pixels on screen).
            const float lightSize = std::max(2.5f / ppm, 1.2f);
            // Slightly larger for emergency strobes.
            const float emerSize  = lightSize * 1.7f;

            // Emergency strobe phase: 4 Hz, two-color alternation.
            const float strobe   = std::fmod(static_cast<float>(now) * 4.0f, 1.0f);
            const bool  strobeA  = strobe < 0.5f;

            // Hard budget — guarantees the loop stays cheap even if everyone
            // is on-screen at very low zoom.
            int budget = 4000;

            primitives.beginFrame(camera);
            for (const auto& v : sim.pool()) {
                if (budget <= 0) break;
                if (v.position.x < vL || v.position.x > vR ||
                    v.position.y < vB || v.position.y > vT) continue;

                const auto& info = simulation::typeInfo(v.type);
                const float halfLen = info.lengthMeters * 0.5f;
                const float cs = std::cos(v.heading);
                const float sn = std::sin(v.heading);
                const glm::vec2 forward{cs, sn};

                // Brake light: red dot at the rear when braking hard.
                if (v.frontGap < 15.0f && !info.emergency) {
                    const glm::vec2 rear = v.position - forward * halfLen;
                    primitives.drawQuad(rear, glm::vec2(lightSize),
                                        glm::vec4(1.0f, 0.20f, 0.10f, 0.95f));
                    --budget;
                }

                // Emergency strobe (ambulance: red/white, police: red/blue).
                if (info.emergency) {
                    glm::vec4 colA, colB;
                    if (v.type == simulation::VehicleType::Ambulance) {
                        colA = {1.00f, 0.10f, 0.10f, 1.0f};
                        colB = {1.00f, 1.00f, 1.00f, 1.0f};
                    } else {
                        colA = {1.00f, 0.10f, 0.10f, 1.0f};
                        colB = {0.10f, 0.30f, 1.00f, 1.0f};
                    }
                    const glm::vec4 c = strobeA ? colA : colB;
                    primitives.drawQuad(v.position, glm::vec2(emerSize), c);
                    --budget;
                }
            }
            primitives.endFrame();
        }

        // Accident markers: pulsing red squares at each wreck location.
        if (showAccidents && graphLoaded && accidentSys.active().size() > 0) {
            V2V_PROF("render/accidents");
            const float ppm   = std::max(camera.zoom(), 1e-6f);
            const float sizePx = 24.0f;
            primitives.beginFrame(camera);
            for (const auto& a : accidentSys.active()) {
                const double age   = now - a.startTime;
                const float  pulse = 0.5f + 0.5f * std::sin(static_cast<float>(age) * 6.28f);
                const float  alpha = 0.55f + 0.40f * pulse;
                const float  size  = sizePx * (1.0f + 0.20f * pulse) / ppm;
                primitives.drawQuad(a.position, glm::vec2(size),
                                    glm::vec4(0.95f, 0.20f, 0.20f, alpha));
            }
            primitives.endFrame();
        }

        // Yielding glow: yellow halo around civilians currently slowed for
        // an emergency vehicle.
        if (graphLoaded) {
            V2V_PROF("render/yield-glow");
            const float ppm = std::max(camera.zoom(), 1e-6f);
            const float size = 30.0f / ppm;
            primitives.beginFrame(camera);
            for (const auto& v : sim.pool()) {
                if (v.yieldingUntil <= now) continue;
                primitives.drawQuad(v.position, glm::vec2(size),
                                    glm::vec4(1.0f, 0.85f, 0.20f, 0.32f));
            }
            primitives.endFrame();
        }

        // V2V broadcast rings (drawn over the links for emphasis).
        if (graphLoaded && broadcastSys.count() > 0) {
            V2V_PROF("render/broadcast");
            bcastR.render(camera, broadcastSys, now);
        }

        // Glow vehicles that received a broadcast in the last 0.8s.
        if (graphLoaded) {
            V2V_PROF("render/broadcast-glow");
            constexpr double kGlowSec = 0.8;
            const float ppm  = std::max(camera.zoom(), 1e-6f);
            const float size = 28.0f / ppm;
            primitives.beginFrame(camera);
            for (const auto& v : sim.pool()) {
                if (v.lastBroadcastReceivedAt < 0.0) continue;
                const double age = now - v.lastBroadcastReceivedAt;
                if (age > kGlowSec) continue;
                const float t = static_cast<float>(age / kGlowSec); // 0..1
                const float alpha = (1.0f - t) * 0.6f;
                primitives.drawQuad(v.position, glm::vec2(size),
                                    glm::vec4(0.45f, 0.95f, 1.0f, alpha));
            }
            primitives.endFrame();
        }

        // Render traffic-light markers (small green/red dots at intersections).
        if (graphLoaded && sim.lights().isEnabled()) {
            V2V_PROF("render/lights");
            const float ppm = std::max(camera.zoom(), 1e-6f);
            const float dotPx = 6.0f;
            const float halfW = dotPx / ppm * 0.5f;
            // Cull to current view to avoid drawing 10k dots at city-wide zoom.
            const float vpHalfW = static_cast<float>(window.width())  * 0.5f / ppm;
            const float vpHalfH = static_cast<float>(window.height()) * 0.5f / ppm;
            const glm::vec2 c = camera.position();
            const float vpL = c.x - vpHalfW, vpR = c.x + vpHalfW;
            const float vpB = c.y - vpHalfH, vpT = c.y + vpHalfH;

            primitives.beginFrame(camera);
            for (const auto& X : sim.lights().intersections()) {
                const float wx = static_cast<float>(X.world.x);
                const float wy = static_cast<float>(X.world.y);
                if (wx < vpL || wx > vpR || wy < vpB || wy > vpT) continue;
                const auto& greenEdge = X.incomingEdges[
                    static_cast<std::size_t>(X.phase)];
                (void)greenEdge;  // marker is per-node, color = "this node has a green light somewhere"
                const glm::vec4 colGreen{0.20f, 0.95f, 0.30f, 0.95f};
                primitives.drawQuad({wx, wy}, glm::vec2(halfW * 2.0f), colGreen);
            }
            primitives.endFrame();
        }

        // Drop selection if the underlying vehicle was despawned by the user.
        if (selectedVehicleId >= static_cast<int>(sim.pool().size())) {
            selectedVehicleId = -1;
        }

        // Highlight selected vehicle + its V2V neighbours + range disc.
        if (selectedVehicleId >= 0 && selectedVehicleId < static_cast<int>(sim.pool().size())) {
            V2V_PROF("render/selection");
            const auto& v = sim.pool()[static_cast<std::size_t>(selectedVehicleId)];
            const float ppm    = std::max(camera.zoom(), 1e-6f);
            primitives.beginFrame(camera);

            // 1) Three concentric pulsing rings expanding outward then fading.
            //    Each ring has a phase-offset so they cascade.
            constexpr float kPulseHz   = 1.1f;
            constexpr float kRingMaxPx = 90.0f;
            for (int k = 0; k < 3; ++k) {
                const float phase = std::fmod(static_cast<float>(now) * kPulseHz
                                              + 0.33f * static_cast<float>(k), 1.0f);
                const float radPx = 20.0f + phase * kRingMaxPx;
                const float alpha = (1.0f - phase) * 0.55f;
                primitives.drawQuad(v.position, glm::vec2(radPx / ppm),
                                    glm::vec4(0.40f, 0.95f, 1.00f, alpha));
            }
            // 2) Crisp inner core (steady).
            primitives.drawQuad(v.position, glm::vec2(38.0f / ppm),
                                glm::vec4(1.0f, 1.0f, 1.0f, 0.45f));

            // 2) Direct V2V neighbours: cyan halo on each peer that has an
            //    active link with the selected vehicle (uses v2vNet.links).
            const std::uint32_t selId = static_cast<std::uint32_t>(selectedVehicleId);
            const float peerHaloPx = 26.0f;
            const float peerSize   = peerHaloPx / ppm;
            for (const auto& l : v2vNet.links()) {
                std::uint32_t other = std::uint32_t(-1);
                if      (l.a == selId) other = l.b;
                else if (l.b == selId) other = l.a;
                else continue;
                if (other >= sim.pool().size()) continue;
                const auto& peer = sim.pool()[other];
                // Alpha follows link strength so weak peers are dimmer.
                const float alpha = 0.30f + 0.45f * l.strength;
                primitives.drawQuad(peer.position, glm::vec2(peerSize),
                                    glm::vec4(0.30f, 0.95f, 1.00f, alpha));
            }

            // 3) V2V range disc: 48 small dots forming a circle at the
            //    vehicle's transmissionRange — visually shows the broadcast
            //    radius.
            constexpr int kDiscSegments = 48;
            const float dotSize = 3.5f / ppm;
            const float R       = v.transmissionRange;
            const float twoPi   = 6.28318530718f;
            for (int k = 0; k < kDiscSegments; ++k) {
                const float a = twoPi * static_cast<float>(k) / kDiscSegments;
                const glm::vec2 p{ v.position.x + R * std::cos(a),
                                   v.position.y + R * std::sin(a) };
                primitives.drawQuad(p, glm::vec2(dotSize),
                                    glm::vec4(0.30f, 0.90f, 1.00f, 0.65f));
            }

            primitives.endFrame();
        }

        // (Mouse marker removed — selection halo is enough visual feedback.)

        // ---- UI ------------------------------------------------------
        {
            V2V_PROF("render/imgui");
            imgui.beginFrame();
            PanelInputs in;
            in.layers.showTiles    = &showTiles;
            in.layers.showGraph    = &showGraph;
            in.layers.showVehicles  = &showVehicles;
            in.layers.showLinks     = &showLinks;
            in.layers.showHeatmap   = &showHeatmap;
            in.layers.showAccidents = &showAccidents;
            in.layers.showOverlay   = &showOverlay;
            in.engine        = graphLoaded ? &sim     : nullptr;
            in.network       = graphLoaded ? &v2vNet  : nullptr;
            in.graph         = graphLoaded ? &graph   : nullptr;
            in.spatial       = graphLoaded ? &spatial : nullptr;
            in.tiles         = &tilesR;
            in.accidents             = &accidentSys;
            in.broadcasts            = &broadcastSys;
            in.vehicleSizeMultiplier = &vehicleSizeMultiplier;
            in.followSelected        = &followSelected;
            in.droneMode             = &droneMode;
            in.tilt3D                = &camera3D;
            in.showVehicleLights     = &showVehicleLights;
            in.timeOfDay             = &timeOfDay;
            static int weatherModeI = 0;
            weatherModeI = static_cast<int>(weather.mode);
            in.weatherMode      = &weatherModeI;
            in.weatherIntensity = &weather.intensity;
            // After draw, sync back the int -> enum.
            // (Done below right after drawPanels(in).)
            in.frameMs           = lastFrameMs;
            in.fps               = lastFps;
            in.selectedVehicleId   = &selectedVehicleId;
            in.onResetCamera       = &resetCameraThunk;
            in.userdata            = &camResetCtx;
            in.onBroadcast         = broadcastThunk;
            in.broadcastUserdata   = &broadcastCtx;
            drawPanels(in);
            // Sync back the weather mode int -> enum after ImGui write.
            weather.mode = static_cast<utils::WeatherMode>(
                std::clamp(weatherModeI, 0, 2));
            // Honour a mini-map click: smooth-jump the camera there.
            if (g_minimapJumpReady) {
                camera.setTargetPosition(g_minimapJumpTarget);
                followSelected      = false;        // user reasserted control
                g_minimapJumpReady  = false;
            }
            imgui.endFrame();
        }

        window.swapBuffers();
        v2v::utils::Profiler::instance().endFrame();
    }

    return 0;
}
catch (const std::exception& e) {
    std::fprintf(stderr, "[FATAL] %s\n", e.what());
    return 1;
}
