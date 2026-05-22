#pragma once

#include <cstddef>
#include <cstdint>

namespace v2v::graph        { class RoadGraph; }
namespace v2v::simulation   { class SimulationEngine; class AccidentSystem; }
namespace v2v::utils        { class SpatialGrid; }
namespace v2v::vnet         { class V2VNetwork; class BroadcastSystem; }
namespace v2v::map          { class TileRenderer; }

namespace v2v::ui {

/**
 * Aggregated state for the per-frame ImGui panels.
 *
 * Pointers are non-owning and may be null when the corresponding feature is
 * not active (e.g. no graph loaded → simulation/v2v pointers are null and the
 * relevant panels degrade gracefully).
 */
struct PanelLayers {
    bool* showTiles     = nullptr;
    bool* showGraph     = nullptr;
    bool* showVehicles  = nullptr;
    bool* showLinks     = nullptr;
    bool* showHeatmap   = nullptr;
    bool* showAccidents = nullptr;
    bool* showOverlay   = nullptr;
};

struct PanelInputs {
    PanelLayers                       layers;
    simulation::SimulationEngine*     engine     = nullptr;
    vnet::V2VNetwork*                 network    = nullptr;
    const graph::RoadGraph*           graph      = nullptr;
    const utils::SpatialGrid*         spatial    = nullptr;
    const map::TileRenderer*          tiles      = nullptr;
    simulation::AccidentSystem*       accidents  = nullptr;
    const vnet::BroadcastSystem*      broadcasts = nullptr;
    float*                            vehicleSizeMultiplier = nullptr;
    bool*                             followSelected = nullptr;   // cinematic camera
    bool*                             droneMode      = nullptr;   // auto-cycling cinematic
    bool*                             showVehicleLights = nullptr;
    float*                            timeOfDay = nullptr;        // 0=midnight, 0.5=noon

    /// Weather mode (0=Clear, 1=Rain, 2=Fog) — encoded as int for ImGui combo.
    int*                              weatherMode = nullptr;
    float*                            weatherIntensity = nullptr;
    double                            frameMs    = 0.0;
    double                            fps        = 0.0;

    /// Currently selected vehicle (id == pool index), or -1 for none.
    /// Bidirectional: the inspector panel can clear it via the Deselect button.
    int*                              selectedVehicleId = nullptr;

    // R / camera-reset callback, invoked from the panel "Reset camera" button.
    void (*onResetCamera)(void*) = nullptr;
    void*                              userdata  = nullptr;

    /// "Send signal" callback, invoked from Selected vehicle panel.
    /// Receives the source vehicle id.
    void (*onBroadcast)(void*, std::uint32_t) = nullptr;
    void*                              broadcastUserdata = nullptr;
};

/// Draws all UI panels for one frame. Must be called between ImGuiLayer's
/// beginFrame / endFrame.
void drawPanels(const PanelInputs& in);

} // namespace v2v::ui
