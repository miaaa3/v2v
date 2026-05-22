#include "ui/Panels.h"

#include "graph/RoadGraph.h"
#include "map/TileRenderer.h"
#include "simulation/AccidentSystem.h"
#include "simulation/SimulationEngine.h"
#include "simulation/Vehicle.h"
#include "simulation/VehiclePool.h"
#include "simulation/VehicleType.h"
#include "utils/Profiler.h"
#include "utils/SpatialGrid.h"
#include "v2v/Broadcast.h"
#include "v2v/V2VNetwork.h"

#include "map/MapProjection.h"

#include <glm/glm.hpp>
#include <imgui.h>

// Defined in main.cpp — receives a world-space target the mini-map was
// clicked at. We forward-declare at global scope so panels can call it.
void minimapJumpRequest(glm::vec2 worldPos);

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace v2v::ui {

namespace {

// Forward decl — radar widget body lives further down the file.
void drawMinimapWidget(const PanelInputs& in, float side);

// Stylised section header: accent bar + uppercase cyan label + underline.
void dockSection(const char* label)
{
    ImGui::Spacing();
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    // Vertical accent bar.
    dl->AddRectFilled(ImVec2(p.x, p.y + 1.0f),
                      ImVec2(p.x + 3.0f, p.y + 15.0f),
                      IM_COL32(20, 200, 240, 255), 1.0f);
    ImGui::Indent(12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.84f, 1.00f, 1.0f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Unindent(12.0f);
    // Thin underline spanning the panel.
    const ImVec2 u = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(u.x, u.y + 1.0f),
                ImVec2(u.x + ImGui::GetContentRegionAvail().x, u.y + 1.0f),
                IM_COL32(40, 72, 110, 170), 1.0f);
    ImGui::Spacing();
}

// Glowing on/off toggle button with a pulsing border + status LED when on.
bool dockToggle(const char* label, bool* value, ImVec2 size)
{
    if (!value) { ImGui::Dummy(size); return false; }
    const bool on = *value;
    const ImVec4 onBg   (0.09f, 0.42f, 0.55f, 1.0f);
    const ImVec4 onHov  (0.14f, 0.58f, 0.74f, 1.0f);
    const ImVec4 onTxt  (0.92f, 0.98f, 1.00f, 1.0f);
    const ImVec4 offBg  (0.12f, 0.16f, 0.24f, 1.0f);
    const ImVec4 offHov (0.18f, 0.24f, 0.34f, 1.0f);
    const ImVec4 offTxt (0.55f, 0.63f, 0.76f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Button,        on ? onBg  : offBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? onHov : offHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  on ? onHov : offHov);
    ImGui::PushStyleColor(ImGuiCol_Text,          on ? onTxt : offTxt);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);

    if (on) {
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        ImDrawList*  dl = ImGui::GetWindowDrawList();
        const float  t  = static_cast<float>(ImGui::GetTime());
        const float  pulse = 0.55f + 0.45f * std::sin(t * 3.2f);
        // Pulsing cyan border.
        dl->AddRect(mn, mx,
                    IM_COL32(70, 220, 255, static_cast<int>(90 + 110 * pulse)),
                    4.0f, 0, 1.6f);
        // Status LED, top-right corner.
        dl->AddCircleFilled(ImVec2(mx.x - 8.0f, mn.y + 8.0f), 3.0f,
                            IM_COL32(120, 240, 255,
                                     static_cast<int>(170 + 85 * pulse)));
    }
    if (clicked) *value = !*value;
    return clicked;
}

// The merged left-side "Control Center": layer toggles, simulation controls,
// tools, and the embedded radar mini-map.
void renderControlDock(const PanelInputs& in)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos (ImVec2(vp->Pos.x + 10.0f, vp->Pos.y + 102.0f),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(322.0f, vp->Size.y - 122.0f),
                             ImGuiCond_FirstUseEver);
    ImGui::Begin("Control Center");

    const float fullW = ImGui::GetContentRegionAvail().x;
    const float colW  = (fullW - 8.0f) * 0.5f;   // 2-column grid
    const ImVec2 btn2(colW, 34.0f);

    // ---- Header banner ---------------------------------------------------
    {
        ImDrawList*  dl = ImGui::GetWindowDrawList();
        const ImVec2 p  = ImGui::GetCursorScreenPos();
        const float  w  = fullW;
        // Gradient strip.
        dl->AddRectFilledMultiColor(
            ImVec2(p.x, p.y), ImVec2(p.x + w, p.y + 34.0f),
            IM_COL32(16, 70, 96, 255),  IM_COL32(10, 30, 48, 255),
            IM_COL32(10, 30, 48, 255),  IM_COL32(16, 70, 96, 255));
        dl->AddRect(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y + 34.0f),
                    IM_COL32(40, 110, 150, 200), 3.0f, 0, 1.0f);
        // Hexagon-ish bullet.
        const ImVec2 c(p.x + 18.0f, p.y + 17.0f);
        dl->AddCircleFilled(c, 6.0f, IM_COL32(20, 200, 240, 255), 6);
        dl->AddCircle      (c, 9.0f, IM_COL32(20, 200, 240, 120), 6, 1.5f);
        // Title.
        dl->AddText(ImVec2(p.x + 34.0f, p.y + 9.0f),
                    IM_COL32(220, 240, 252, 255), "CONTROL CENTER");
        ImGui::Dummy(ImVec2(w, 40.0f));
    }

    // ---- LAYERS — toggle grid -------------------------------------------
    dockSection("LAYERS");
    dockToggle("Map",      in.layers.showTiles,     btn2); ImGui::SameLine();
    dockToggle("Roads",    in.layers.showGraph,     btn2);
    dockToggle("Vehicles", in.layers.showVehicles,  btn2); ImGui::SameLine();
    dockToggle("V2V Links",in.layers.showLinks,     btn2);
    dockToggle("Heatmap",  in.layers.showHeatmap,   btn2); ImGui::SameLine();
    dockToggle("Alerts",   in.layers.showAccidents, btn2);
    dockToggle("Lights",   in.showVehicleLights,    btn2); ImGui::SameLine();
    dockToggle("Console",  in.layers.showOverlay,   btn2);

    // ---- SIMULATION ------------------------------------------------------
    dockSection("SIMULATION");
    if (in.engine) {
        bool paused = in.engine->isPaused();
        if (dockToggle(paused ? "Paused" : "Running", &paused, btn2))
            in.engine->setPaused(paused);
        ImGui::SameLine();
        if (ImGui::Button("Step +1s", btn2)) {
            const bool wp = in.engine->isPaused();
            in.engine->setPaused(false);
            in.engine->step(1.0f);
            in.engine->setPaused(wp);
        }

        float ts = in.engine->timeScale();
        ImGui::SetNextItemWidth(fullW);
        if (ImGui::SliderFloat("##ts", &ts, 0.0625f, 16.0f, "Speed  %.2fx",
                               ImGuiSliderFlags_Logarithmic))
            in.engine->setTimeScale(ts);
        const float tb = (fullW - 16.0f) / 3.0f;
        if (ImGui::Button("1x",  ImVec2(tb,0))) in.engine->setTimeScale(1.0f);
        ImGui::SameLine();
        if (ImGui::Button("4x",  ImVec2(tb,0))) in.engine->setTimeScale(4.0f);
        ImGui::SameLine();
        if (ImGui::Button("16x", ImVec2(tb,0))) in.engine->setTimeScale(16.0f);

        int count = static_cast<int>(in.engine->pool().size());
        ImGui::SetNextItemWidth(fullW);
        if (ImGui::SliderInt("##pop", &count, 0, 5000, "Vehicles  %d")) {
            const int cur   = static_cast<int>(in.engine->pool().size());
            const int delta = count - cur;
            if (delta > 0)      in.engine->spawn  (static_cast<std::size_t>( delta));
            else if (delta < 0) in.engine->despawn(static_cast<std::size_t>(-delta));
        }
        const float qb = (fullW - 24.0f) / 4.0f;
        if (ImGui::Button("-100", ImVec2(qb,0))) in.engine->despawn(100);
        ImGui::SameLine();
        if (ImGui::Button("+100", ImVec2(qb,0))) in.engine->spawn(100);
        ImGui::SameLine();
        if (ImGui::Button("+1k",  ImVec2(qb,0))) in.engine->spawn(1000);
        ImGui::SameLine();
        if (ImGui::Button("Clear",ImVec2(qb,0)))
            in.engine->despawn(in.engine->pool().size());
    } else {
        ImGui::TextDisabled("No simulation loaded.");
    }

    // ---- TOOLS -----------------------------------------------------------
    dockSection("TOOLS");
    dockToggle("Follow Car", in.followSelected, btn2);
    ImGui::SameLine();
    dockToggle("Drone Cam",  in.droneMode,      btn2);
    if (in.accidents) {
        if (ImGui::Button("Trigger Accident (X)", ImVec2(fullW, 30.0f)))
            in.accidents->forceTrigger();
    }
    if (in.onResetCamera &&
        ImGui::Button("Reset Camera (R)", ImVec2(fullW, 0)))
        in.onResetCamera(in.userdata);

    // ---- ENVIRONMENT -----------------------------------------------------
    dockSection("ENVIRONMENT");
    if (in.timeOfDay) {
        ImGui::SetNextItemWidth(fullW);
        ImGui::SliderFloat("##tod", in.timeOfDay, 0.0f, 1.0f,
                           (*in.timeOfDay < 0.25f || *in.timeOfDay > 0.75f)
                           ? "Time  night" : "Time  day");
    }
    if (in.weatherMode && in.weatherIntensity) {
        const char* items[] = { "Clear", "Rain", "Fog" };
        ImGui::SetNextItemWidth(fullW);
        ImGui::Combo("##weather", in.weatherMode, items, 3);
        ImGui::SetNextItemWidth(fullW);
        ImGui::SliderFloat("##wint", in.weatherIntensity, 0.0f, 1.0f,
                           "Intensity  %.2f");
    }
    if (in.vehicleSizeMultiplier) {
        ImGui::SetNextItemWidth(fullW);
        ImGui::SliderFloat("##vsz", in.vehicleSizeMultiplier, 0.5f, 4.0f,
                           "Car size  %.2fx");
    }

    // ---- PRESETS ---------------------------------------------------------
    dockSection("QUALITY PRESET");
    auto preset = [&](bool heat, bool links, bool lights) {
        if (in.layers.showHeatmap) *in.layers.showHeatmap = heat;
        if (in.layers.showLinks)   *in.layers.showLinks   = links;
        if (in.showVehicleLights)  *in.showVehicleLights  = lights;
        if (in.layers.showTiles)   *in.layers.showTiles   = true;
        if (in.layers.showGraph)   *in.layers.showGraph   = true;
    };
    const float pb = (fullW - 24.0f) / 4.0f;
    if (ImGui::Button("Ultra", ImVec2(pb,0)))  preset(true,  true,  true);
    ImGui::SameLine();
    if (ImGui::Button("High",  ImVec2(pb,0)))  preset(false, true,  true);
    ImGui::SameLine();
    if (ImGui::Button("Med",   ImVec2(pb,0)))  preset(false, true,  false);
    ImGui::SameLine();
    if (ImGui::Button("Perf",  ImVec2(pb,0)))  preset(false, false, false);

    // ---- EMBEDDED RADAR --------------------------------------------------
    dockSection("RADAR");
    drawMinimapWidget(in, fullW);

    // ---- KEYBOARD SHORTCUTS ---------------------------------------------
    dockSection("SHORTCUTS");
    if (ImGui::BeginTable("##keys", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        auto keyRow = [](const char* k, const char* desc) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.84f, 1.0f, 1.0f));
            ImGui::TextUnformatted(k);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", desc);
        };
        keyRow("Left-drag",  "Pan the map");
        keyRow("Wheel",      "Zoom on cursor");
        keyRow("Left-click", "Select a vehicle");
        keyRow("SPACE",      "Pause / resume");
        keyRow("+ / -",      "Time scale x2 / /2");
        keyRow("0",          "Time scale 1x");
        keyRow("R",          "Reset camera");
        keyRow("G/M/V/L",    "Toggle graph/map/cars/links");
        keyRow("H",          "Toggle density heatmap");
        keyRow("X",          "Trigger an accident");
        keyRow("F1",         "Toggle stdout overlay");
        keyRow("ESC",        "Quit");
        ImGui::EndTable();
    }

    ImGui::End();
}

// --- Content-only section bodies (no Begin/End) — used by the merged
//     "Telemetry" panel so several feeds share one window. -----------------

void bodySimulation(const PanelInputs& in)
{
    if (!in.engine || !in.graph) {
        ImGui::TextDisabled("No simulation loaded.");
        return;
    }
    ImGui::Text("Graph: %zu nodes, %zu edges",
                in.graph->nodeCount(), in.graph->edgeCount());
    ImGui::Text("Vehicles: %zu", in.engine->pool().size());
    ImGui::Text("Pathfinds (last step): %zu", in.engine->lastStepPathfinds());
    ImGui::Text("Edge transitions: %zu", in.engine->lastStepEdgeTransitions());
    if (in.spatial) {
        ImGui::Text("SpatialGrid: %d x %d  (cell %.0f m)",
                    in.spatial->cols(), in.spatial->rows(), in.spatial->cellSize());
        ImGui::Text("Entries: %zu  max-bucket: %zu",
                    in.spatial->entryCount(), in.spatial->maxBucketSize());
    }
}

void bodyV2V(const PanelInputs& in)
{
    if (!in.network) { ImGui::TextDisabled("No V2V network."); return; }
    float r = in.network->maxRange();
    if (ImGui::SliderFloat("Max range (m)", &r, 50.0f, 1000.0f, "%.0f m")) {
        in.network->setMaxRange(r);
    }
    ImGui::Text("Active links: %zu",       in.network->linkCount());
    ImGui::Text("Avg link strength: %.3f", in.network->averageStrength());
}

void bodyPerf(const PanelInputs& in)
{
    ImGui::Text("Frame: %.2f ms  (%.0f FPS)", in.frameMs, in.fps);
    if (in.tiles) {
        ImGui::Text("OSM zoom: %d   tiles visible: %zu",
                    in.tiles->lastFrameZoom(),
                    in.tiles->lastFrameVisibleTileCount());
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Profiler (1-s window, sorted by avg cost)");
    if (ImGui::BeginTable("prof", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("Section");
        ImGui::TableSetupColumn("ms/frame");
        ImGui::TableSetupColumn("calls");
        ImGui::TableHeadersRow();
        for (const auto& s : utils::Profiler::instance().samples()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%.3f", s.avgMsPerFrame);
            ImGui::TableNextColumn(); ImGui::Text("%u",    s.lastFrameCalls);
        }
        ImGui::EndTable();
    }
}

// Forward declaration — body lives in the vehicle-inspector helpers below.
const char* prettyTypePrefix(simulation::VehicleType t);

// ---------- Top stats bar (dashboard header) ------------------------------

// ---------- Mini-map radar (embeddable widget) ----------------------------

// Draws the radar mini-map into the *current* window at the cursor position,
// occupying a `side` x `side` square. No Begin/End — caller owns the window.
void drawMinimapWidget(const PanelInputs& in, float side)
{
    if (!in.engine || !in.graph || in.engine->pool().empty()) {
        ImGui::Dummy(ImVec2(side, side));
        return;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 a{ origin.x, origin.y };
    const ImVec2 b{ origin.x + side, origin.y + side };

    // World bounds of the loaded graph (with a small margin so things at the
    // edge are still visible).
    const glm::dvec4 wb = in.graph->worldBounds();
    const double wL = wb.x, wR = wb.z;
    const double wB = wb.y, wT = wb.w;
    const double ww = std::max(1.0, wR - wL);
    const double wh = std::max(1.0, wT - wB);
    auto worldToScreen = [&](glm::vec2 p) {
        const float u = static_cast<float>((p.x - wL) / ww);
        const float v = 1.0f - static_cast<float>((p.y - wB) / wh);
        return ImVec2(a.x + u * side, a.y + v * side);
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background.
    dl->AddRectFilled(a, b, IM_COL32(10, 16, 28, 240), 4.0f);
    dl->AddRect      (a, b, IM_COL32(60, 90,140, 200), 4.0f, 0, 1.0f);

    // ---- Rotating radar sweep beam (cyan, fades behind itself) ---------
    {
        const ImVec2 ctr(a.x + side * 0.5f, a.y + side * 0.5f);
        const float  R = side * 0.5f - 4.0f;
        const float  t = static_cast<float>(ImGui::GetTime());
        constexpr float kSweepRadPerSec = 1.6f;       // ~95°/s
        const float ang  = std::fmod(t * kSweepRadPerSec,
                                     6.28318530718f);
        constexpr int   kTrailSteps = 24;
        constexpr float kTrailRad   = 0.65f;            // ~37° trailing wedge
        // Wedge filled triangles fading toward the trailing edge.
        for (int i = 0; i < kTrailSteps; ++i) {
            const float t0 = float(i)     / float(kTrailSteps);
            const float t1 = float(i + 1) / float(kTrailSteps);
            const float a0 = ang - kTrailRad * t0;
            const float a1 = ang - kTrailRad * t1;
            const ImVec2 p0 = ImVec2(ctr.x + R * std::cos(a0),
                                     ctr.y + R * std::sin(a0));
            const ImVec2 p1 = ImVec2(ctr.x + R * std::cos(a1),
                                     ctr.y + R * std::sin(a1));
            const int alpha = static_cast<int>(110 * (1.0f - t0));
            dl->AddTriangleFilled(ctr, p0, p1,
                                  IM_COL32(20, 200, 240, alpha));
        }
        // Bright leading edge.
        const ImVec2 tip(ctr.x + R * std::cos(ang),
                         ctr.y + R * std::sin(ang));
        dl->AddLine(ctr, tip, IM_COL32(140, 240, 255, 230), 1.5f);
        // Concentric range rings for "radar grid" feel.
        for (int r = 1; r <= 3; ++r) {
            const float rr = R * (static_cast<float>(r) / 3.0f);
            dl->AddCircle(ctr, rr, IM_COL32(60, 120, 170, 90), 0, 1.0f);
        }
    }

    // Vehicles as 1-px dots, colored by type.
    auto typeColor = [](simulation::VehicleType t) -> ImU32 {
        switch (t) {
            case simulation::VehicleType::Car:       return IM_COL32(77, 199, 244, 230);
            case simulation::VehicleType::Truck:     return IM_COL32(140,143,159, 220);
            case simulation::VehicleType::Bus:       return IM_COL32(247,198, 51, 240);
            case simulation::VehicleType::Ambulance: return IM_COL32(245,245,245, 245);
            case simulation::VehicleType::Police:    return IM_COL32( 64,114,250, 245);
            default: return IM_COL32(200,200,200,200);
        }
    };
    for (const auto& v : in.engine->pool()) {
        const ImVec2 sp = worldToScreen(v.position);
        dl->AddRectFilled(ImVec2(sp.x - 1, sp.y - 1),
                          ImVec2(sp.x + 1, sp.y + 1),
                          typeColor(v.type));
    }

    // Accident dots (red).
    if (in.accidents) {
        for (const auto& acc : in.accidents->active()) {
            const ImVec2 sp = worldToScreen(acc.position);
            dl->AddCircleFilled(sp, 3.5f, IM_COL32(240, 60, 60, 230));
        }
    }

    // Selected vehicle: larger cyan ring.
    if (in.selectedVehicleId && *in.selectedVehicleId >= 0 &&
        *in.selectedVehicleId < static_cast<int>(in.engine->pool().size()))
    {
        const auto& v = in.engine->pool()[
            static_cast<std::size_t>(*in.selectedVehicleId)];
        const ImVec2 sp = worldToScreen(v.position);
        dl->AddCircle(sp, 5.0f, IM_COL32(80, 220, 255, 255), 0, 2.0f);
    }

    // Current camera view as a cyan rectangle.
    {
        const glm::vec2 c   = ImGui::GetIO().DisplaySize.x > 0 ? glm::vec2(0,0) : glm::vec2(0,0);
        (void)c;
        // We don't have the camera here directly through PanelInputs, but we
        // can re-derive its window via tile renderer's last frame if needed.
        // Simpler: leave the rectangle out for now; the selected-vehicle
        // marker plus the visible dot cluster is enough orientation.
    }

    // Allow click-to-jump.
    ImGui::InvisibleButton("##mmclick", ImVec2(side, side));
    if (ImGui::IsItemClicked() && in.engine) {
        const ImVec2 mp = ImGui::GetMousePos();
        const float u = (mp.x - a.x) / side;
        const float v = (mp.y - a.y) / side;
        if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
            const glm::vec2 target(
                static_cast<float>(wL + u * ww),
                static_cast<float>(wB + (1.0f - v) * wh));
            ::minimapJumpRequest(target);
        }
    }

    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Click to jump · %zu vehicles",
                        in.engine->pool().size());
}

// Rolling buffer + animated counter for one metric in the top bar.
struct StatTrack {
    static constexpr std::size_t kHistory = 90;     // ~1.5 s @ 60 fps
    std::array<float, kHistory> hist{};
    std::size_t                 head = 0;
    float                       displayed = 0.0f;   // lerped toward target
    double                      lastSampleT = 0.0;
    float                       minV = 0.0f, maxV = 1.0f;

    void push(float v) {
        hist[head] = v;
        head = (head + 1) % kHistory;
        // Track min/max for adaptive sparkline scaling.
        float mn = hist[0], mx = hist[0];
        for (auto x : hist) { if (x < mn) mn = x; if (x > mx) mx = x; }
        minV = mn; maxV = mx;
    }

    void tick(float target, float dt, float halfLifeSec = 0.18f) {
        if (dt <= 0.0f) { displayed = target; return; }
        const float alpha = 1.0f - std::pow(0.5f, dt / halfLifeSec);
        displayed += (target - displayed) * alpha;
    }
};

void renderTopStatsBar(const PanelInputs& in)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    constexpr float kBarHeight = 92.0f;
    ImGui::SetNextWindowPos (ImVec2(vp->Pos.x, vp->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, kBarHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(20.0f, 10.0f));
    ImGui::Begin("##v2v_topbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Custom dark background with a soft cyan bottom-edge glow.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a(vp->Pos.x, vp->Pos.y);
        const ImVec2 b(vp->Pos.x + vp->Size.x, vp->Pos.y + kBarHeight);
        dl->AddRectFilled(a, b, IM_COL32(8, 14, 26, 250));
        dl->AddRectFilledMultiColor(
            ImVec2(a.x, b.y - 3.0f), b,
            IM_COL32(14, 200, 240, 0), IM_COL32(14, 200, 240, 0),
            IM_COL32(14, 200, 240, 180), IM_COL32(14, 200, 240, 180));
    }

    const ImVec4 cyan  (0.20f, 0.85f, 1.00f, 1.0f);
    const ImVec4 cyanS (0.40f, 0.95f, 1.00f, 1.0f);
    const ImVec4 green (0.42f, 0.95f, 0.50f, 1.0f);
    const ImVec4 amber (1.00f, 0.85f, 0.30f, 1.0f);
    const ImVec4 red   (0.98f, 0.35f, 0.35f, 1.0f);
    const ImVec4 dim   (0.55f, 0.62f, 0.76f, 1.0f);

    // ---- Brand block ------------------------------------------------------
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(2.1f);
    ImGui::PushStyleColor(ImGuiCol_Text, cyanS);
    ImGui::TextUnformatted("V2V");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, dim);
    ImGui::TextUnformatted("Smart Transportation");
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 26.0f);

    // Vertical divider.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetContentRegionAvail().y;
        dl->AddLine(ImVec2(p.x, p.y),
                    ImVec2(p.x, p.y + h),
                    IM_COL32(60, 90, 140, 110), 1.0f);
        ImGui::Dummy(ImVec2(1.0f, 0.0f));
        ImGui::SameLine(0.0f, 22.0f);
    }

    // ---- Live "ONLINE" status pill with pulsing dot. ---------------------
    {
        const double t = ImGui::GetTime();
        const float  pulse = 0.55f + 0.45f * std::sin(static_cast<float>(t) * 3.0f);
        ImDrawList*  dl   = ImGui::GetWindowDrawList();
        const ImVec2 p    = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(p.x + 6.0f, p.y + 14.0f), 5.0f,
                            IM_COL32(60, 220, 110,
                                     static_cast<int>(170 + 80 * pulse)));
        ImGui::Dummy(ImVec2(14.0f, 0.0f));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, green);
        ImGui::SetWindowFontScale(1.05f);
        ImGui::TextUnformatted("ONLINE");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 26.0f);
    }

    // ---- Stat cards ------------------------------------------------------
    static StatTrack sVeh, sLinks, sFps, sSig, sBcast, sAlerts;
    static double  sLastT       = 0.0;
    static double  sSampleAccum = 0.0;
    const double now = ImGui::GetTime();
    const float  dt  = (sLastT > 0.0) ? static_cast<float>(now - sLastT) : 0.0f;
    sLastT = now;

    const float nVeh   = in.engine     ? static_cast<float>(in.engine->pool().size())   : 0.0f;
    const float nLinks = in.network    ? static_cast<float>(in.network->linkCount())    : 0.0f;
    const float fps    = static_cast<float>(in.fps);
    const float avgStr = in.network    ? in.network->averageStrength() : 0.0f;
    const float nBcast = in.broadcasts ? static_cast<float>(in.broadcasts->count()) : 0.0f;
    const float nAcc   = in.accidents  ? static_cast<float>(in.accidents->active().size()) : 0.0f;

    // Lerp the displayed numbers each frame, push to sparkline history at 30 Hz.
    sVeh.tick   (nVeh,   dt);  sLinks.tick (nLinks, dt);
    sFps.tick   (fps,    dt, 0.10f);
    sSig.tick   (avgStr, dt);  sBcast.tick (nBcast, dt);
    sAlerts.tick(nAcc,   dt, 0.10f);
    sSampleAccum += dt;
    if (sSampleAccum >= 1.0 / 30.0) {
        sSampleAccum = 0.0;
        sVeh.push   (nVeh);   sLinks.push (nLinks);
        sFps.push   (fps);    sSig.push   (avgStr);
        sBcast.push (nBcast); sAlerts.push(nAcc);
    }

    auto card = [&](const char* label, StatTrack& s,
                    const char* fmt, ImVec4 col, bool integer = false)
    {
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, dim);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::SetWindowFontScale(1.55f);
        char vbuf[32];
        if (integer) std::snprintf(vbuf, sizeof(vbuf), fmt,
                                   static_cast<int>(std::round(s.displayed)));
        else         std::snprintf(vbuf, sizeof(vbuf), fmt, s.displayed);
        ImGui::TextUnformatted(vbuf);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        // Sparkline — unique ID per card to avoid ImGui ID collisions.
        const float lo = s.minV, hi = std::max(s.maxV, lo + 1e-3f);
        char spId[40];
        std::snprintf(spId, sizeof(spId), "##sp_%s", label);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_PlotLines, col);
        ImGui::PlotLines(spId, s.hist.data(),
                         static_cast<int>(s.hist.size()),
                         static_cast<int>(s.head),
                         nullptr, lo, hi, ImVec2(96.0f, 18.0f));
        ImGui::PopStyleColor(2);
        ImGui::EndGroup();
    };

    card("VEHICLES",  sVeh,    "%d",    cyan,                    true);
    ImGui::SameLine(0, 22.0f);
    card("LINKS",     sLinks,  "%d",    green,                   true);
    ImGui::SameLine(0, 22.0f);
    card("FPS",       sFps,    "%.0f",  fps >= 55 ? green : amber);
    ImGui::SameLine(0, 22.0f);
    card("AVG SIG",   sSig,    "%.2f",  amber);
    ImGui::SameLine(0, 22.0f);
    card("BROADCAST", sBcast,  "%d",    cyan,                    true);
    ImGui::SameLine(0, 22.0f);

    // Alerts card pulses when > 0.
    if (nAcc > 0) {
        const float ph = 0.55f + 0.45f *
                          std::sin(static_cast<float>(now) * 5.5f);
        const ImVec4 col(0.98f, 0.30f + 0.10f * ph, 0.30f + 0.10f * ph, 1.0f);
        card("ALERTS",  sAlerts, "%d", col,    true);
    } else {
        card("ALERTS",  sAlerts, "%d", green,  true);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ---------- Live event feed -----------------------------------------------

struct EventEntry {
    double      time;
    ImVec4      color;
    std::string text;
};

static std::vector<EventEntry> g_eventFeed;
static std::size_t             g_lastAccidentCount = 0;
static std::size_t             g_lastBroadcastCount = 0;
static int                     g_lastSelectionForFeed = -1;

void pushEvent(double t, ImVec4 col, std::string s)
{
    if (g_eventFeed.size() >= 80) {
        g_eventFeed.erase(g_eventFeed.begin(),
                          g_eventFeed.begin() + 20); // batch trim
    }
    g_eventFeed.push_back({t, col, std::move(s)});
}

void renderEventFeed(const PanelInputs& in)
{
    if (!in.engine) return;
    const double now = in.engine->now();

    // Collect new events from the world state.
    const ImVec4 cAcc   {0.95f, 0.45f, 0.30f, 1.0f};
    const ImVec4 cBcast {0.30f, 0.85f, 1.00f, 1.0f};
    const ImVec4 cSel   {1.00f, 0.85f, 0.30f, 1.0f};

    if (in.accidents) {
        const std::size_t total = in.accidents->total();
        while (g_lastAccidentCount < total) {
            ++g_lastAccidentCount;
            char b[128];
            std::snprintf(b, sizeof(b), "Accident #%zu — V2V emergency alert dispatched",
                          g_lastAccidentCount);
            pushEvent(now, cAcc, b);
        }
    }
    if (in.broadcasts) {
        const std::size_t total = in.broadcasts->count();
        if (total > g_lastBroadcastCount) {
            char b[64];
            std::snprintf(b, sizeof(b), "V2V signal — %zu rings propagating", total);
            pushEvent(now, cBcast, b);
        }
        g_lastBroadcastCount = total;
    }
    if (in.selectedVehicleId && *in.selectedVehicleId != g_lastSelectionForFeed) {
        g_lastSelectionForFeed = *in.selectedVehicleId;
        if (g_lastSelectionForFeed >= 0 &&
            g_lastSelectionForFeed < static_cast<int>(in.engine->pool().size()))
        {
            const auto& v = in.engine->pool()[
                static_cast<std::size_t>(g_lastSelectionForFeed)];
            char b[96];
            std::snprintf(b, sizeof(b), "Inspecting vehicle %s_%04d",
                          prettyTypePrefix(v.type), g_lastSelectionForFeed);
            pushEvent(now, cSel, b);
        }
    }

    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos (ImVec2(vp->Pos.x + vp->Size.x - 340.0f,
                                        vp->Pos.y + 502.0f),
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(330.0f, 230.0f), ImGuiCond_FirstUseEver);
    }
    ImGui::Begin("Event feed");
    ImGui::TextDisabled("Live system events");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) g_eventFeed.clear();
    ImGui::Separator();
    ImGui::BeginChild("##evfeed", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (auto it = g_eventFeed.rbegin(); it != g_eventFeed.rend(); ++it) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.55f, 0.65f, 1.0f));
        ImGui::Text("t=%6.1fs", it->time);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(it->color, "•");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", it->text.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}

void bodyAccidents(const PanelInputs& in)
{
    if (!in.accidents) { ImGui::TextDisabled("No accident system."); return; }

    bool en = in.accidents->isEnabled();
    if (ImGui::Checkbox("Auto-trigger", &en)) in.accidents->setEnabled(en);

    float r = in.accidents->ratePerSec();
    if (ImGui::SliderFloat("Rate (/sec)", &r, 0.0f, 1.0f, "%.2f")) {
        in.accidents->setRatePerSec(r);
    }

    float d = in.accidents->duration();
    if (ImGui::SliderFloat("Wreck duration (s)", &d, 2.0f, 60.0f, "%.0f")) {
        in.accidents->setDuration(d);
    }

    if (ImGui::Button("Trigger now (X)", ImVec2(-1, 0))) {
        in.accidents->forceTrigger();
    }
    ImGui::Text("Active wrecks: %zu", in.accidents->active().size());
    ImGui::Text("Total ever:   %zu",  in.accidents->total());
}

void bodyLegend(const PanelInputs& in)
{
    if (!in.engine) { ImGui::TextDisabled("No simulation."); return; }

    // Count vehicles per type for "you have X of these" right column.
    int counts[static_cast<int>(simulation::VehicleType::Count)] = {};
    for (const auto& v : in.engine->pool()) {
        const auto idx = static_cast<int>(v.type);
        if (idx >= 0 && idx < static_cast<int>(simulation::VehicleType::Count)) counts[idx]++;
    }

    if (ImGui::BeginTable("legend", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("");        // color swatch
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("Count");
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(simulation::VehicleType::Count); ++i) {
            const auto t     = static_cast<simulation::VehicleType>(i);
            const auto& info = simulation::typeInfo(t);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec4 col(info.color.r, info.color.g, info.color.b, 1.0f);
            ImGui::ColorButton((std::string("##c") + info.name).c_str(),
                               col,
                               ImGuiColorEditFlags_NoTooltip |
                               ImGuiColorEditFlags_NoPicker  |
                               ImGuiColorEditFlags_NoBorder,
                               ImVec2(22.0f, 14.0f));

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(info.name);
            if (info.emergency) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.30f, 1.0f), "*");
            }

            ImGui::TableNextColumn();
            ImGui::Text("%.1f × %.1f m", info.lengthMeters, info.widthMeters);

            ImGui::TableNextColumn();
            ImGui::Text("%d", counts[i]);
        }
        ImGui::EndTable();
    }

    ImGui::TextDisabled("* = emergency vehicle (extended V2V range, priority)");
}

// ---------- Merged "Telemetry" panel --------------------------------------
// Combines Simulation / V2V / Accidents / Vehicle types / Performance into a
// single collapsible-section window, so the screen isn't littered with small
// floating panels.
void renderTelemetryPanel(const PanelInputs& in)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos (ImVec2(vp->Pos.x + vp->Size.x - 340.0f,
                                    vp->Pos.y + 102.0f),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330.0f, 392.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Telemetry");

    if (ImGui::CollapsingHeader("Simulation",   ImGuiTreeNodeFlags_DefaultOpen))
        bodySimulation(in);
    if (ImGui::CollapsingHeader("V2V network",  ImGuiTreeNodeFlags_DefaultOpen))
        bodyV2V(in);
    if (ImGui::CollapsingHeader("Accidents"))
        bodyAccidents(in);
    if (ImGui::CollapsingHeader("Vehicle types"))
        bodyLegend(in);
    if (ImGui::CollapsingHeader("Performance"))
        bodyPerf(in);

    ImGui::End();
}

// Per-selected-vehicle UI state (speed history graph). Reset when selection
// changes. Keyed by current selectedId so switching vehicles resets the trace.
struct InspectorState {
    int                    lastSelected = -1;
    std::array<float, 120> speedHistory{};  // ~2 s @ 60 fps
    std::size_t            historyPos = 0;
    double                 lastSampleTime = 0.0;
};
static InspectorState g_inspector{};

const char* prettyTypePrefix(simulation::VehicleType t)
{
    switch (t) {
        case simulation::VehicleType::Car:       return "CAR";
        case simulation::VehicleType::Truck:     return "TRK";
        case simulation::VehicleType::Bus:       return "BUS";
        case simulation::VehicleType::Ambulance: return "AMB";
        case simulation::VehicleType::Police:    return "POL";
        default: return "VEH";
    }
}

inline const char* protocolName(simulation::VehicleType t)
{
    if (t == simulation::VehicleType::Ambulance ||
        t == simulation::VehicleType::Police)
        return "5G NR-V2X";
    if (t == simulation::VehicleType::Bus || t == simulation::VehicleType::Truck)
        return "DSRC (IEEE 802.11p)";
    return "C-V2X PC5";
}

inline const char* cardinal(float headingRad)
{
    const float pi = 3.14159265358979323846f;
    const float twoPi = 2 * pi;
    float h = std::fmod(headingRad, twoPi);
    if (h < 0) h += twoPi;
    const float deg = h * 180.0f / pi;
    if (deg < 22.5f  || deg >= 337.5f) return "E";
    if (deg <  67.5f) return "NE";
    if (deg < 112.5f) return "N";
    if (deg < 157.5f) return "NW";
    if (deg < 202.5f) return "W";
    if (deg < 247.5f) return "SW";
    if (deg < 292.5f) return "S";
    return "SE";
}

// Derive the AI decision + reason from the vehicle's current state. Purely
// computed each frame; nothing is stored.
struct Decision { const char* what; const char* why; ImVec4 color; };

inline Decision deriveDecision(const simulation::Vehicle& v,
                               const simulation::SimulationEngine& eng)
{
    const double now = eng.now();
    if (v.wreckedUntil > now)
        return { "STOPPED — wrecked",  "Involved in an accident",
                 ImVec4(0.95f, 0.30f, 0.30f, 1.0f) };
    if (v.yieldingUntil > now)
        return { "Yielding",            "Emergency vehicle nearby (within 45 m)",
                 ImVec4(1.0f, 0.85f, 0.20f, 1.0f) };
    if (v.frontGap < 6.0f)
        return { "Hard braking",        "Vehicle within 6 m ahead",
                 ImVec4(0.95f, 0.45f, 0.30f, 1.0f) };
    if (v.frontGap < 20.0f)
        return { "Cautious follow",     "Vehicle close ahead (gap < 20 m)",
                 ImVec4(0.95f, 0.85f, 0.30f, 1.0f) };
    if (v.currentEdge != graph::INVALID_EDGE &&
        !eng.lights().isGreen(v.currentEdge) && v.edgeProgress > 0.78f)
        return { "Stopped at red light", "Traffic signal red at next intersection",
                 ImVec4(0.95f, 0.30f, 0.30f, 1.0f) };
    if (v.path.empty() && v.destination != graph::INVALID_NODE)
        return { "Computing route (A*)", "No path queued — pathfinding pending",
                 ImVec4(0.45f, 0.85f, 1.0f, 1.0f) };
    if (v.speed > 25.0f)
        return { "Cruising",            "Clear road, near speed limit",
                 ImVec4(0.45f, 0.95f, 0.50f, 1.0f) };
    return { "Driving",                 "Normal flow",
             ImVec4(0.95f, 0.95f, 0.95f, 1.0f) };
}

inline void sectionBasic(int selId, const simulation::Vehicle& v,
                         const simulation::SimulationEngine& eng)
{
    char idBuf[32];
    std::snprintf(idBuf, sizeof(idBuf), "%s_%04d",
                  prettyTypePrefix(v.type), selId);

    const auto& tinfo = simulation::typeInfo(v.type);
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "ID");
    ImGui::SameLine(110); ImGui::TextUnformatted(idBuf);

    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "Type");
    ImGui::SameLine(110);
    ImGui::TextColored(ImVec4(tinfo.color.r, tinfo.color.g, tinfo.color.b, 1.0f),
                       "%s%s", tinfo.name, tinfo.emergency ? "  ⚠ emergency" : "");

    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "Size");
    ImGui::SameLine(110); ImGui::Text("%.1f × %.1f m   v_max %.0f km/h",
                                       tinfo.lengthMeters, tinfo.widthMeters, tinfo.maxSpeedKmh);

    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "Speed");
    ImGui::SameLine(110); ImGui::Text("%.1f m/s  (%.0f km/h)", v.speed, v.speed * 3.6f);

    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "Accel");
    ImGui::SameLine(110); ImGui::Text("%+.2f m/s²", v.acceleration);

    const float headingDeg = v.heading * 180.0f / 3.14159265358979323846f;
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "Heading");
    ImGui::SameLine(110); ImGui::Text("%.0f°   (%s)", headingDeg, cardinal(v.heading));

    const double age = std::max(0.0, eng.now() - v.spawnTime);
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "Age");
    ImGui::SameLine(110); ImGui::Text("%.0f s", age);
}

inline void sectionGPS(const simulation::Vehicle& v,
                       const graph::RoadGraph* graph)
{
    const glm::dvec2 ll = map::MapProjection::worldToLatLon(
        glm::dvec2(v.position.x, v.position.y));
    ImGui::Text("Latitude : %.6f°", ll.x);
    ImGui::Text("Longitude: %.6f°", ll.y);
    ImGui::Text("World   : %.1f, %.1f m", v.position.x, v.position.y);
    ImGui::Text("Distance traveled: %.0f m  (%.2f km)",
                v.distanceTraveledM, v.distanceTraveledM / 1000.0f);

    if (graph && v.currentEdge != graph::INVALID_EDGE) {
        const auto& e = graph->edge(v.currentEdge);
        ImGui::Text("On edge: %u  (%.0f%% of %.0f m)",
                    v.currentEdge, v.edgeProgress * 100.0f, e.length);
        ImGui::Text("Road type: %s   limit %.0f km/h",
                    graph::roadTypeName(e.type), e.maxspeed);
        ImGui::Text("Next node: %u   (path: %zu hops queued)",
                    e.to, v.path.size());
    }
    if (v.destination != graph::INVALID_NODE) {
        ImGui::Text("Destination: node %u", v.destination);
    }
}

inline void sectionV2V(int selId, const simulation::Vehicle& v,
                       const vnet::V2VNetwork* network)
{
    int neighbors  = 0;
    float bestS    = 0.0f;
    float sumS     = 0.0f;
    float sumDist  = 0.0f;
    if (network) {
        for (const auto& l : network->links()) {
            if (static_cast<int>(l.a) == selId || static_cast<int>(l.b) == selId) {
                ++neighbors;
                sumS    += l.strength;
                sumDist += l.distance;
                if (l.strength > bestS) bestS = l.strength;
            }
        }
    }
    const float avgS    = neighbors ? sumS    / neighbors : 0.0f;
    const float avgDist = neighbors ? sumDist / neighbors : 0.0f;
    // Estimate latency: c (radio) ~3e8 m/s but real DSRC latency is ~1-50 ms
    // dominated by MAC. We fake a realistic 5–25 ms range.
    const float latencyMs = 4.0f + 0.06f * avgDist;
    // Simulated packet loss ∝ 1 - avg strength (cap 30%).
    const float lossPct   = std::min(30.0f, (1.0f - avgS) * 30.0f);

    ImGui::Text("Connected vehicles : %d", neighbors);
    ImGui::Text("Communication range: %.0f m", v.transmissionRange);
    ImGui::Text("Best link strength : %.3f", bestS);
    ImGui::Text("Avg  link strength : %.3f", avgS);
    ImGui::Text("Avg  peer distance : %.0f m", avgDist);
    ImGui::Separator();
    ImGui::Text("Packets sent      : %u", v.packetsSent);
    ImGui::Text("Packets received  : %u", v.packetsReceived);
    ImGui::Text("Est. latency      : %.1f ms", latencyMs);
    ImGui::Text("Est. packet loss  : %.1f %%", lossPct);
}

inline void sectionAI(const simulation::Vehicle& v,
                      const simulation::SimulationEngine& eng)
{
    const Decision d = deriveDecision(v, eng);
    ImGui::TextColored(d.color, "Current decision: %s", d.what);
    ImGui::TextDisabled("Reason: %s", d.why);
    ImGui::Separator();
    ImGui::Text("Front-vehicle gap : %.1f m",
                std::isfinite(v.frontGap) ? v.frontGap : 999.0f);
    ImGui::Text("Yielding?         : %s", (v.yieldingUntil > eng.now()) ? "yes" : "no");
    ImGui::Text("Wrecked?          : %s", (v.wreckedUntil > eng.now()) ? "yes" : "no");
}

inline void sectionSensors(const simulation::Vehicle& v,
                           const utils::SpatialGrid* spatial)
{
    // Simulated "radar / lidar" based on real proximity data.
    int peersWithin30 = 0;
    int peersWithin60 = 0;
    float closest = 1e9f;
    if (spatial) {
        static thread_local std::vector<std::uint32_t> tmp;
        tmp.clear();
        spatial->queryRadius(v.position, 60.0f, tmp);
        for (auto id : tmp) {
            if (id == v.id) continue;
            ++peersWithin60;
            // distance is queried but we don't have positions here; use frontGap
            // as a stand-in for distance to closest in front.
        }
        tmp.clear();
        spatial->queryRadius(v.position, 30.0f, tmp);
        for (auto id : tmp) {
            if (id == v.id) continue;
            ++peersWithin30;
        }
        closest = std::isfinite(v.frontGap) ? v.frontGap : 60.0f;
    }
    ImGui::Text("Radar (60 m): %d targets", peersWithin60);
    ImGui::Text("Lidar (30 m): %d targets", peersWithin30);
    ImGui::Text("Closest obstacle ahead: %.1f m", closest);
    ImGui::Text("Camera detection: %s",
                peersWithin30 > 0 ? "vehicles visible" : "clear");
    ImGui::Text("Blind-spot check: %s", "clear");
}

inline void sectionNetwork(const simulation::Vehicle& v)
{
    // Deterministic "noise" per vehicle id so values are stable across frames.
    auto h = static_cast<float>((v.id * 2654435761u) & 0xFFFF) / 65535.0f;
    const float bandwidth = 0.4f + 0.5f * h;             // Mbps
    const float stability = 0.85f + 0.13f * (1.0f - h);  // 0..1
    const int   retrans   = 1 + static_cast<int>(h * 4); // 1..4
    const int   dropped   = static_cast<int>(h * 3);     // 0..3

    ImGui::Text("Protocol     : %s", protocolName(v.type));
    ImGui::Text("Bandwidth    : %.2f Mbps", bandwidth);
    ImGui::Text("Stability    : %.0f %%", stability * 100.0f);
    ImGui::Text("Retransmits  : %d",   retrans);
    ImGui::Text("Dropped pkts : %d",   dropped);
}

inline void sectionSecurity(const simulation::Vehicle& v)
{
    auto h = static_cast<float>((v.id * 1099511628211ull) & 0xFFFF) / 65535.0f;
    const float trust = 0.88f + 0.12f * h;
    const bool encrypted = true;
    const bool suspicious = (h > 0.97f); // ~3% suspicious

    if (suspicious)
        ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                           "⚠ Suspicious behaviour pattern detected");
    else
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.50f, 1.0f),
                           "Trusted node");

    ImGui::Text("Trust score      : %.2f / 1.00", trust);
    ImGui::Text("Encryption       : %s", encrypted ? "AES-128-GCM ✓" : "off");
    ImGui::Text("Fake-pkt alerts  : %d", suspicious ? 2 : 0);
    ImGui::Text("Cert. validity   : OK (24 h)");
}

// Draw a circular progress gauge: outer arc shows the value vs max.
inline void drawGauge(const char* label, float pct, ImVec4 fillColor,
                      const char* unit = "%%", float radius = 38.0f)
{
    pct = std::clamp(pct, 0.0f, 100.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + radius + 6.0f, origin.y + radius + 6.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    constexpr float kStart = -1.5707963f * 0.55f - 1.5707963f * 1.45f; // start angle (top-left of arc, ~210°)
    constexpr float kEnd   = -1.5707963f * 0.55f + 1.5707963f * 1.45f; // end   angle  (top-right of arc, ~-30°)
    const float ang = kStart + (kEnd - kStart) * (pct / 100.0f);

    // Background track.
    dl->PathArcTo(center, radius, kStart, kEnd, 48);
    dl->PathStroke(IM_COL32(40, 60, 90, 220), 0, 4.0f);
    // Filled portion.
    dl->PathArcTo(center, radius, kStart, ang, 48);
    dl->PathStroke(IM_COL32(static_cast<int>(fillColor.x * 255),
                            static_cast<int>(fillColor.y * 255),
                            static_cast<int>(fillColor.z * 255), 240),
                   0, 4.0f);
    // Inner big number.
    char num[16];
    std::snprintf(num, sizeof(num), "%.0f%s", pct, unit);
    const ImVec2 ts = ImGui::CalcTextSize(num);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
        IM_COL32(230, 240, 250, 255), num);

    // Label below.
    ImGui::Dummy(ImVec2(radius * 2.0f + 12.0f, radius * 2.0f + 12.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.62f, 0.78f, 1.0f));
    const ImVec2 ls = ImGui::CalcTextSize(label);
    ImGui::SetCursorScreenPos(ImVec2(center.x - ls.x * 0.5f,
                                     origin.y + radius * 2.0f + 14.0f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

inline void sectionHealth(const simulation::Vehicle& v)
{
    const float fuelPct = std::clamp(100.0f - v.distanceTraveledM * 0.0008f, 0.0f, 100.0f);
    const float engineC = 80.0f + v.speed * 0.6f;
    const float tirePct = std::clamp(100.0f - v.distanceTraveledM * 0.0005f, 70.0f, 100.0f);
    const float engineNorm = std::clamp((engineC - 60.0f) / 60.0f, 0.0f, 1.0f) * 100.0f;

    // Three circular gauges side by side.
    auto colorFor = [](float pct, bool reverse = false) {
        const float t = reverse ? (1.0f - pct / 100.0f) : (pct / 100.0f);
        if (t > 0.66f) return ImVec4(0.45f, 0.95f, 0.50f, 1.0f);  // green
        if (t > 0.33f) return ImVec4(1.00f, 0.85f, 0.30f, 1.0f);  // amber
        return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);                  // red
    };

    const ImVec2 start = ImGui::GetCursorScreenPos();
    drawGauge("Fuel",   fuelPct,   colorFor(fuelPct));
    ImGui::SetCursorScreenPos(ImVec2(start.x + 102.0f, start.y));
    drawGauge("Tires",  tirePct,   colorFor(tirePct));
    ImGui::SetCursorScreenPos(ImVec2(start.x + 204.0f, start.y));
    drawGauge("Engine", engineNorm,colorFor(engineNorm, /*reverse=*/true), "%%");

    ImGui::Separator();
    ImGui::Text("Engine temp.     : %.0f °C", engineC);
    ImGui::Text("Brake system     : %s", "OK");
    ImGui::Text("System temp.     : %.0f °C", 28.0f + v.speed * 0.1f);
}

inline void sectionPrediction(const simulation::Vehicle& v,
                              const graph::RoadGraph*    graph)
{
    if (!graph || v.path.empty() || v.pathPos >= v.path.size()) {
        ImGui::TextDisabled("No queued route to predict on.");
        return;
    }
    float totalLen = 0.0f;
    float totalTime = 0.0f;
    for (std::size_t k = v.pathPos; k < v.path.size(); ++k) {
        const auto& e = graph->edge(v.path[k]);
        totalLen  += e.length;
        const float vMs = std::max(1.0f, e.maxspeed / 3.6f);
        totalTime += e.length / vMs;
    }
    ImGui::Text("Hops remaining : %zu", v.path.size() - v.pathPos);
    ImGui::Text("Distance       : %.0f m  (%.2f km)", totalLen, totalLen / 1000.0f);
    ImGui::Text("ETA            : %.0f s  (≈ %.1f min)", totalTime, totalTime / 60.0f);

    // Collision probability heuristic: function of frontGap and speed.
    const float gap   = std::isfinite(v.frontGap) ? v.frontGap : 999.0f;
    const float speed = std::max(0.1f, v.speed);
    const float ttc   = gap / speed;                       // time-to-collision
    const float pCol  = std::clamp(1.0f - ttc / 5.0f, 0.0f, 1.0f);
    ImGui::Text("Time-to-collision: %.1f s", ttc);
    ImGui::Text("Collision risk   :");
    ImGui::SameLine();
    if (pCol < 0.3f) ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.45f, 1.0f), "low");
    else if (pCol < 0.7f) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "medium");
    else ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.30f, 1.0f), "HIGH");
}

inline void sectionActivity(const simulation::Vehicle& v,
                            const simulation::SimulationEngine& eng,
                            double now)
{
    if (v.lastBroadcastReceivedAt > 0) {
        const double age = now - v.lastBroadcastReceivedAt;
        if (age < 5.0)
            ImGui::Text("📨 V2V packet received   %.1f s ago", age);
    }
    if (v.lastRelayedAt > 0) {
        const double age = now - v.lastRelayedAt;
        if (age < 5.0)
            ImGui::Text("📡 Relayed broadcast      %.1f s ago", age);
    }
    if (v.yieldingUntil > now) {
        ImGui::Text("🚨 Yielding to emergency  %.1f s remaining",
                    v.yieldingUntil - now);
    }
    if (v.wreckedUntil > now) {
        ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                           "💥 Wrecked              %.1f s remaining",
                           v.wreckedUntil - now);
    }
    if (v.currentEdge != graph::INVALID_EDGE &&
        !eng.lights().isGreen(v.currentEdge) && v.edgeProgress > 0.78f)
    {
        ImGui::Text("🚦 Stopped at red light");
    }
    if (v.path.empty() && v.destination != graph::INVALID_NODE) {
        ImGui::Text("🧭 Awaiting A* path");
    }
}

void renderSelectedVehiclePanel(const PanelInputs& in)
{
    if (!in.engine || !in.selectedVehicleId) return;

    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos (ImVec2(vp->Pos.x + vp->Size.x - 360.0f,
                                        vp->Pos.y + 102.0f),
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350.0f, vp->Size.y - 130.0f),
                                 ImGuiCond_FirstUseEver);
    }
    ImGui::Begin("Vehicle Inspector");

    int& selId = *in.selectedVehicleId;
    const auto& pool = in.engine->pool();

    if (selId < 0 || selId >= static_cast<int>(pool.size())) {
        ImGui::TextDisabled("Click a vehicle on the map to inspect it.");
        ImGui::End();
        return;
    }

    const auto& v = pool[static_cast<std::size_t>(selId)];

    // Refresh the speed-history graph buffer.
    const double now = in.engine->now();
    if (g_inspector.lastSelected != selId) {
        g_inspector.lastSelected = selId;
        g_inspector.speedHistory.fill(0.0f);
        g_inspector.historyPos = 0;
    }
    if (now - g_inspector.lastSampleTime > 0.05) {
        g_inspector.speedHistory[g_inspector.historyPos] = v.speed * 3.6f;
        g_inspector.historyPos = (g_inspector.historyPos + 1) %
                                 g_inspector.speedHistory.size();
        g_inspector.lastSampleTime = now;
    }

    // Header row with broadcast button.
    if (in.onBroadcast) {
        if (ImGui::Button("📡  Send V2V signal", ImVec2(-100.0f, 0))) {
            in.onBroadcast(in.broadcastUserdata, static_cast<std::uint32_t>(selId));
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Deselect", ImVec2(-1.0f, 0))) selId = -1;

    ImGui::Separator();

    // Tabbed inspector (Overview / Signals / Messages / Sensors / AI /
    // Security / Health) — matches the dashboard reference layout.
    if (ImGui::BeginTabBar("##inspector_tabs",
                           ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if (ImGui::BeginTabItem("Overview")) {
            sectionBasic(selId, v, *in.engine);
            ImGui::Spacing();
            ImGui::SeparatorText("Position");
            sectionGPS(v, in.graph);
            ImGui::Spacing();
            ImGui::SeparatorText("Activity");
            sectionActivity(v, *in.engine, now);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Signals")) {
            sectionV2V(selId, v, in.network);
            ImGui::Spacing();
            ImGui::SeparatorText("Speed history (km/h)");
            ImGui::PlotLines("##speed",
                             g_inspector.speedHistory.data(),
                             static_cast<int>(g_inspector.speedHistory.size()),
                             static_cast<int>(g_inspector.historyPos),
                             nullptr,
                             0.0f, 160.0f,
                             ImVec2(-1, 80));

            // ---- Network graph: selected vehicle + its V2V neighbours --
            ImGui::Spacing();
            ImGui::SeparatorText("V2V network topology");
            if (in.network && in.engine) {
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const float  side   = std::min(ImGui::GetContentRegionAvail().x, 220.0f);
                ImDrawList*  dl     = ImGui::GetWindowDrawList();
                const ImVec2 a = origin;
                const ImVec2 b = ImVec2(origin.x + side, origin.y + side);

                dl->AddRectFilled(a, b, IM_COL32(10, 16, 28, 220), 4.0f);
                dl->AddRect      (a, b, IM_COL32(60, 90,140, 180), 4.0f, 0, 1.0f);

                const ImVec2 ctr(a.x + side * 0.5f, a.y + side * 0.5f);
                const float  R   = side * 0.40f;
                const float  selR= 8.0f;
                const float  nbR = 6.0f;

                // Collect neighbours of selected, up to 8.
                struct Nb { std::uint32_t id; float strength, dist; };
                std::vector<Nb> nbs;
                const auto selU = static_cast<std::uint32_t>(selId);
                for (const auto& l : in.network->links()) {
                    Nb x;
                    if      (l.a == selU) { x = {l.b, l.strength, l.distance}; }
                    else if (l.b == selU) { x = {l.a, l.strength, l.distance}; }
                    else continue;
                    if (x.id >= in.engine->pool().size()) continue;
                    nbs.push_back(x);
                    if (nbs.size() >= 8) break;
                }

                // Draw lines first.
                const float dAng = 6.28318530718f /
                                   std::max<std::size_t>(1, nbs.size());
                for (std::size_t i = 0; i < nbs.size(); ++i) {
                    const float ang = -1.5707963f + dAng * static_cast<float>(i);
                    const ImVec2 nb(ctr.x + R * std::cos(ang),
                                    ctr.y + R * std::sin(ang));
                    const int   alpha   = static_cast<int>(60 + 180 * nbs[i].strength);
                    const ImU32 lineCol = (nbs[i].strength > 0.66f)
                        ? IM_COL32( 80, 230, 110, alpha)
                        : (nbs[i].strength > 0.33f)
                        ? IM_COL32(255, 200,  60, alpha)
                        : IM_COL32(255,  90,  80, alpha);
                    dl->AddLine(ctr, nb, lineCol,
                                1.0f + 1.5f * nbs[i].strength);
                }
                // Draw nodes on top.
                for (std::size_t i = 0; i < nbs.size(); ++i) {
                    const float ang = -1.5707963f + dAng * static_cast<float>(i);
                    const ImVec2 nb(ctr.x + R * std::cos(ang),
                                    ctr.y + R * std::sin(ang));
                    const auto& vt = in.engine->pool()[nbs[i].id].type;
                    auto col = simulation::typeInfo(vt).color;
                    const ImU32 c32 = IM_COL32(
                        static_cast<int>(col.r * 255),
                        static_cast<int>(col.g * 255),
                        static_cast<int>(col.b * 255), 255);
                    dl->AddCircleFilled(nb, nbR, c32);
                    dl->AddCircle      (nb, nbR + 1.5f,
                                        IM_COL32(220, 240, 255, 180), 0, 1.0f);
                    // Strength label.
                    char sb[8];
                    std::snprintf(sb, sizeof(sb), "%.2f", nbs[i].strength);
                    dl->AddText(ImVec2(nb.x + 8.0f, nb.y - 6.0f),
                                IM_COL32(180, 200, 220, 200), sb);
                }
                // Selected at center with cyan glow.
                dl->AddCircleFilled(ctr, selR + 4.0f, IM_COL32(20, 200, 240,  60));
                dl->AddCircleFilled(ctr, selR + 2.0f, IM_COL32(20, 200, 240, 120));
                dl->AddCircleFilled(ctr, selR,        IM_COL32(180, 240, 255, 255));

                ImGui::Dummy(ImVec2(side, side));
                ImGui::TextDisabled("Up to 8 direct V2V peers. Edge thickness/color = signal strength.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Network")) {
            sectionNetwork(v);
            ImGui::Spacing();
            ImGui::SeparatorText("Trajectory prediction");
            sectionPrediction(v, in.graph);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sensors")) {
            sectionSensors(v, in.spatial);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("AI")) {
            sectionAI(v, *in.engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Security")) {
            sectionSecurity(v);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Health")) {
            sectionHealth(v);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace

void drawPanels(const PanelInputs& in)
{
    // Layout (defaults, user can still drag):
    //   ┌─────────────── top stats bar (full width) ───────────────┐
    //   │ Controls │            map / world view          │ Vehicle │
    //   │ Layers   │                                      │Inspector│
    //   │ Telemetry│                                      │         │
    //   │ Event    │                              ┌───────┴─────────┤
    //   │ feed     │                              │     Mini-map     │
    //   └──────────┴──────────────────────────────┴──────────────────┘
    renderTopStatsBar         (in);   // pinned, full width
    renderControlDock         (in);   // left dock: toggles + sim + tools + radar
    renderTelemetryPanel      (in);   // right column, top (merged feeds)
    renderEventFeed           (in);   // right column, bottom
    renderSelectedVehiclePanel(in);   // right column, on vehicle select
}

} // namespace v2v::ui
