#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace v2v::utils {

/**
 * Minimal scoped profiler.
 *
 * Usage:
 *     {
 *         V2V_PROF("simulation/step");
 *         engine.step(dt);
 *     }
 *
 * Each named section accumulates wall-clock time (ms) and call count. Call
 * `Profiler::instance().endFrame()` once per frame; every second the running
 * averages are exposed via `samples()`. Keys are compared by string content,
 * so dynamic names are safe but the literal-pointer fast path is preferred.
 */
class Profiler {
public:
    struct Sample {
        std::string name;
        double      avgMsPerFrame = 0.0;
        double      lastFrameMs   = 0.0;
        std::uint32_t lastFrameCalls = 0;
    };

    static Profiler& instance();

    void begin(const char* name);
    void end  (const char* name);

    /// Call once at the end of each frame. Folds per-frame totals into a 1-s
    /// running window and clears per-frame counters.
    void endFrame();

    /// Snapshot of the most recently completed 1-s window.
    const std::vector<Sample>& samples() const { return m_lastWindow; }

private:
    using Clock = std::chrono::high_resolution_clock;

    struct Section {
        std::uint64_t  frameNs   = 0;
        std::uint32_t  frameCalls = 0;
        std::uint64_t  windowNs  = 0;
        std::uint32_t  windowCalls = 0;
        std::uint32_t  windowFrames = 0;
        Clock::time_point t0{};   // for the currently-open scope (if any)
        bool           open = false;
    };

    std::unordered_map<std::string, Section> m_sections;
    std::vector<Sample>                      m_lastWindow;

    Clock::time_point m_windowStart = Clock::now();
};

/// RAII guard that brackets a named section. Use the V2V_PROF macro.
class ProfileScope {
public:
    explicit ProfileScope(const char* name) : m_name(name) {
        Profiler::instance().begin(m_name);
    }
    ~ProfileScope() { Profiler::instance().end(m_name); }
    ProfileScope(const ProfileScope&)            = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
private:
    const char* m_name;
};

} // namespace v2v::utils

#define V2V_PROF_CONCAT_(a, b) a##b
#define V2V_PROF_CONCAT(a, b)  V2V_PROF_CONCAT_(a, b)
#define V2V_PROF(name) \
    ::v2v::utils::ProfileScope V2V_PROF_CONCAT(_v2v_prof_, __LINE__)(name)
