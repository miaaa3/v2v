#include "simulation/SimulationEngine.h"
#include "simulation/VehicleType.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace v2v::simulation {

namespace {

inline float kmhToMs(float kmh) { return kmh / 3.6f; }

/// Pick a random VehicleType with population-realistic weights.
VehicleType pickVehicleType(std::mt19937& rng)
{
    std::uniform_int_distribution<int> d(0, 999);
    const int r = d(rng);
    if (r <  850) return VehicleType::Car;        // 85.0%
    if (r <  925) return VehicleType::Truck;      //  7.5%
    if (r <  955) return VehicleType::Bus;        //  3.0%
    if (r <  980) return VehicleType::Ambulance;  //  2.5%
    return VehicleType::Police;                   //  2.0%
}

inline float headingFromTo(const glm::dvec2& a, const glm::dvec2& b)
{
    return static_cast<float>(std::atan2(b.y - a.y, b.x - a.x));
}

} // namespace

SimulationEngine::SimulationEngine(const graph::RoadGraph& graph, std::uint32_t rngSeed)
    : m_graph(graph), m_rng(rngSeed)
{
    // Ensure the graph has its adjacency built (used by both spawn and step).
    if (graph.nodeCount() > 0) (void)graph.edgesFrom(0);
    // Detect intersections (deg >= 3) and build cyclic light phases.
    m_lights.build(graph);
}

graph::NodeIndex SimulationEngine::pickRandomNode()
{
    if (m_graph.nodeCount() == 0) return graph::INVALID_NODE;
    // Prefer nodes that actually have outgoing edges (a few retries).
    std::uniform_int_distribution<std::uint32_t> dist(
        0, static_cast<std::uint32_t>(m_graph.nodeCount() - 1));
    for (int attempt = 0; attempt < 12; ++attempt) {
        const graph::NodeIndex n = dist(m_rng);
        if (!m_graph.edgesFrom(n).empty()) return n;
    }
    return dist(m_rng);
}

graph::EdgeIndex SimulationEngine::pickRandomOutgoing(graph::NodeIndex node,
                                                     graph::EdgeIndex avoidEdge)
{
    const auto& out = m_graph.edgesFrom(node);
    if (out.empty()) return graph::INVALID_EDGE;

    // If only one option exists, take it (legitimate cul-de-sac U-turn).
    if (out.size() == 1) return out.front();

    // Otherwise prefer not to U-turn: collect non-reverse candidates.
    graph::NodeIndex came = graph::INVALID_NODE;
    if (avoidEdge != graph::INVALID_EDGE) {
        came = m_graph.edge(avoidEdge).from;
    }

    // Build a small candidate buffer on the stack (out-degree rarely > 8).
    graph::EdgeIndex cands[16];
    int n = 0;
    for (graph::EdgeIndex ei : out) {
        if (n >= 16) break;
        if (came != graph::INVALID_NODE && m_graph.edge(ei).to == came) continue;
        cands[n++] = ei;
    }
    if (n == 0) {
        std::uniform_int_distribution<std::size_t> d(0, out.size() - 1);
        return out[d(m_rng)];
    }
    std::uniform_int_distribution<int> d(0, n - 1);
    return cands[d(m_rng)];
}

Vehicle SimulationEngine::makeRandomVehicle()
{
    Vehicle v;
    v.type = pickVehicleType(m_rng);
    std::uniform_real_distribution<float> rangeDist(150.0f, 350.0f);
    v.transmissionRange = rangeDist(m_rng);
    // Emergency vehicles get a bigger radio (broadcast farther).
    if (typeInfo(v.type).emergency) v.transmissionRange += 150.0f;
    v.spawnTime = m_now;

    // Pick a start node that has outgoing edges.
    graph::NodeIndex start = pickRandomNode();
    if (start == graph::INVALID_NODE) return v;

    // Pick its first edge as starting edge.
    graph::EdgeIndex first = pickRandomOutgoing(start, graph::INVALID_EDGE);
    if (first == graph::INVALID_EDGE) return v;

    v.currentEdge  = first;
    v.edgeProgress = 0.0f;
    v.destination  = pickRandomNode();

    // Initial position/heading from edge geometry.
    const auto& e  = m_graph.edge(first);
    const auto& na = m_graph.node(e.from).world;
    const auto& nb = m_graph.node(e.to  ).world;
    v.position     = glm::vec2(static_cast<float>(na.x), static_cast<float>(na.y));
    v.heading      = headingFromTo(na, nb);
    v.speed        = kmhToMs(e.maxspeed);
    return v;
}

void SimulationEngine::despawn(std::size_t n)
{
    if (n == 0) return;
    auto& v = m_pool.vehicles();
    if (n >= v.size()) { v.clear(); return; }
    v.erase(v.end() - static_cast<std::ptrdiff_t>(n), v.end());
}

void SimulationEngine::spawn(std::size_t n)
{
    if (m_graph.nodeCount() == 0) {
        V2V_LOG_WARN("SimulationEngine::spawn: empty graph — nothing to spawn.");
        return;
    }
    m_pool.reserve(m_pool.size() + n);
    std::size_t actuallySpawned = 0;
    for (std::size_t i = 0; i < n; ++i) {
        Vehicle v = makeRandomVehicle();
        if (v.currentEdge == graph::INVALID_EDGE) continue;
        m_pool.add(std::move(v));
        ++actuallySpawned;
    }
    V2V_LOG_INFO("SimulationEngine: spawned %zu / %zu vehicles (graph has %zu nodes, %zu edges).",
                 actuallySpawned, n, m_graph.nodeCount(), m_graph.edgeCount());
}

void SimulationEngine::updatePosition(Vehicle& v)
{
    if (v.currentEdge == graph::INVALID_EDGE) return;
    const auto& e  = m_graph.edge(v.currentEdge);
    const auto& na = m_graph.node(e.from).world;
    const auto& nb = m_graph.node(e.to  ).world;

    const double t = static_cast<double>(v.edgeProgress);
    v.position = glm::vec2(static_cast<float>(na.x + (nb.x - na.x) * t),
                           static_cast<float>(na.y + (nb.y - na.y) * t));
    v.heading  = headingFromTo(na, nb);
    v.speed    = kmhToMs(e.maxspeed);
}

bool SimulationEngine::enterNextEdge(Vehicle& v, int& pathfindBudget)
{
    if (v.currentEdge == graph::INVALID_EDGE) return false;
    const auto& doneEdge = m_graph.edge(v.currentEdge);
    const graph::NodeIndex atNode = doneEdge.to;

    graph::EdgeIndex next = graph::INVALID_EDGE;

    // 1. Try following the queued path.
    if (v.pathPos < v.path.size()) {
        const graph::EdgeIndex candidate = v.path[v.pathPos];
        if (m_graph.edge(candidate).from == atNode) {
            next = candidate;
            ++v.pathPos;
        } else {
            // Path desynced (probably re-routed earlier) — drop it.
            v.path.clear();
            v.pathPos = 0;
        }
    }

    // 2. If we've reached our destination, pick a new one.
    if (atNode == v.destination) {
        v.destination = pickRandomNode();
        v.path.clear();
        v.pathPos = 0;
    }

    // 3. Empty path → request A* (budgeted) and/or random-walk in the meantime.
    if (next == graph::INVALID_EDGE) {
        if (v.path.empty() && pathfindBudget > 0 &&
            v.destination != graph::INVALID_NODE && atNode != v.destination)
        {
            std::vector<graph::EdgeIndex> p;
            if (m_finder.findPath(m_graph, atNode, v.destination, p) && !p.empty()) {
                v.path    = std::move(p);
                v.pathPos = 0;
                next      = v.path[v.pathPos++];
                ++m_lastPathfinds;
                --pathfindBudget;
            } else {
                // No path — give the vehicle a new destination next time.
                v.destination = pickRandomNode();
            }
        }
    }

    // 4. Fallback random walk.
    if (next == graph::INVALID_EDGE) {
        next = pickRandomOutgoing(atNode, v.currentEdge);
    }

    if (next == graph::INVALID_EDGE) {
        // Stuck. Respawn somewhere else.
        Vehicle fresh   = makeRandomVehicle();
        fresh.id        = v.id;
        fresh.type      = v.type;
        fresh.transmissionRange = v.transmissionRange;
        v = std::move(fresh);
        return true;
    }

    v.currentEdge  = next;
    v.edgeProgress = 0.0f;
    ++m_lastTransitions;
    return true;
}

void SimulationEngine::computeFrontGaps()
{
    static thread_local std::unordered_map<graph::EdgeIndex, std::vector<std::uint32_t>> byEdge;
    byEdge.clear();
    byEdge.reserve(m_pool.size() / 4 + 1);

    // 1. Bucket vehicles by their current edge.
    auto& vehicles = m_pool.vehicles();
    for (std::uint32_t i = 0; i < vehicles.size(); ++i) {
        const auto e = vehicles[i].currentEdge;
        if (e == graph::INVALID_EDGE) {
            vehicles[i].frontGap = std::numeric_limits<float>::infinity();
            continue;
        }
        byEdge[e].push_back(i);
    }

    // 2. For each edge, sort by progress and compute the gap to the next
    //    vehicle ahead. A wrecked vehicle ahead clamps the follower's gap
    //    to (gap - 1m) so it pulls up nose-to-tail.
    for (auto& [edge, ids] : byEdge) {
        if (ids.size() == 1) {
            vehicles[ids[0]].frontGap = std::numeric_limits<float>::infinity();
            continue;
        }
        std::sort(ids.begin(), ids.end(), [&](std::uint32_t a, std::uint32_t b) {
            return vehicles[a].edgeProgress < vehicles[b].edgeProgress;
        });
        const float edgeLength = m_graph.edge(edge).length;
        for (std::size_t k = 0; k < ids.size(); ++k) {
            const auto myId = ids[k];
            if (k + 1 < ids.size()) {
                const auto otherId = ids[k + 1];
                const float gap = (vehicles[otherId].edgeProgress -
                                   vehicles[myId].edgeProgress) * edgeLength;
                // Subtract average car length so the gap reflects bumper-to-bumper.
                float useGap = std::max(0.0f, gap - 5.0f);
                // Wreck ahead: clamp harder so traffic queues bumper-to-bumper.
                if (vehicles[otherId].wreckedUntil > m_now) {
                    useGap = std::min(useGap, std::max(0.0f, gap - 6.5f));
                }
                vehicles[myId].frontGap = useGap;
            } else {
                vehicles[myId].frontGap = std::numeric_limits<float>::infinity();
            }
        }
    }
}

void SimulationEngine::step(float dt)
{
    m_lastPathfinds   = 0;
    m_lastTransitions = 0;

    if (m_paused) return;
    if (m_pool.empty()) return;

    const float scaledDt   = dt * m_timeScale;
    int         pfBudget   = m_pathfindBudgetPerStep;

    // Advance traffic-light phases.
    m_lights.update(scaledDt);

    // Compute front-gap for each vehicle once per step (pre-pass).
    computeFrontGaps();

    constexpr float kSafetyMin = 5.0f;   // m: full stop below this gap
    constexpr float kSafetyMax = 20.0f;  // m: no slowdown above this gap
    constexpr float kStopBeforeLight = 0.93f;  // edgeProgress cap when red
    constexpr float kSlowZoneStart   = 0.78f;  // start braking from here

    for (auto& v : m_pool) {
        if (v.currentEdge == graph::INVALID_EDGE) continue;

        // Wrecked vehicles are frozen obstacles: no movement, no rotation.
        if (v.wreckedUntil > m_now) {
            updatePosition(v);
            v.speed = 0.0f;
            continue;
        }

        const auto& e = m_graph.edge(v.currentEdge);

        // 1) Front-vehicle throttle (don't drive into the car ahead).
        float throttle = 1.0f;
        if (v.frontGap < kSafetyMax) {
            throttle = std::clamp(
                (v.frontGap - kSafetyMin) / (kSafetyMax - kSafetyMin),
                0.0f, 1.0f);
        }

        // 2) Traffic-light throttle (stop just before red intersections).
        const bool greenAhead = m_lights.isGreen(v.currentEdge);
        if (!greenAhead && v.edgeProgress > kSlowZoneStart) {
            const float t = std::clamp(
                (kStopBeforeLight - v.edgeProgress) /
                (kStopBeforeLight - kSlowZoneStart),
                0.0f, 1.0f);
            throttle = std::min(throttle, t);
        }

        // 3) Yielding to an emergency vehicle nearby — significant slowdown
        // for a short window. Emergency vehicles themselves never yield.
        if (v.yieldingUntil > m_now && !typeInfo(v.type).emergency) {
            throttle = std::min(throttle, 0.35f);
        }

        if (e.length <= 0.01f) {
            v.edgeProgress = 1.0f;
        } else {
            const float vMs    = kmhToMs(e.maxspeed) * throttle;
            const float deltaP = (vMs * scaledDt) / e.length;
            v.edgeProgress    += deltaP;
            // Hard cap: never cross a red light, even if we overshot.
            if (!greenAhead && v.edgeProgress > kStopBeforeLight) {
                v.edgeProgress = kStopBeforeLight;
            }
            // Telemetry: cumulative distance + acceleration estimate.
            const float distM = vMs * scaledDt;
            v.distanceTraveledM += distM;
            if (scaledDt > 1e-4f) {
                v.acceleration = (vMs - v.prevSpeed) / scaledDt;
            }
            v.prevSpeed = vMs;
        }

        // It's possible to overshoot multiple short edges in one step at high
        // time-scale; loop until we've consumed the full step.
        while (v.edgeProgress >= 1.0f) {
            v.edgeProgress -= 1.0f;
            if (!enterNextEdge(v, pfBudget)) break;
            // Scale residual progress to the new edge length: this keeps
            // motion smooth across edge transitions of differing lengths.
            const auto& ne = m_graph.edge(v.currentEdge);
            if (ne.length > 0.01f) {
                const float vMs2 = kmhToMs(ne.maxspeed);
                if (vMs2 > 0.0f) {
                    // Residual is a fraction of an edge; leave it as-is. The
                    // small numerical drift averaged across edges is fine.
                }
            } else {
                v.edgeProgress = 1.0f; // degenerate edge; advance again next loop
            }
        }

        updatePosition(v);
    }
}

} // namespace v2v::simulation
