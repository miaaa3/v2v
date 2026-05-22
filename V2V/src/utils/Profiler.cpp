#include "utils/Profiler.h"

#include <algorithm>

namespace v2v::utils {

Profiler& Profiler::instance()
{
    static Profiler p;
    return p;
}

void Profiler::begin(const char* name)
{
    auto& s = m_sections[name];
    if (s.open) return;       // re-entrant begin without a matching end; skip
    s.t0   = Clock::now();
    s.open = true;
}

void Profiler::end(const char* name)
{
    auto it = m_sections.find(name);
    if (it == m_sections.end() || !it->second.open) return;
    const auto dt   = Clock::now() - it->second.t0;
    const auto ns   = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    it->second.frameNs   += ns;
    it->second.windowNs  += ns;
    ++it->second.frameCalls;
    ++it->second.windowCalls;
    it->second.open = false;
}

void Profiler::endFrame()
{
    // Fold per-frame totals into the running window.
    for (auto& [name, s] : m_sections) {
        if (s.frameCalls > 0) {
            ++s.windowFrames;
        }
        s.frameNs    = 0;
        s.frameCalls = 0;
    }

    const auto now = Clock::now();
    if (now - m_windowStart >= std::chrono::seconds(1)) {
        // Roll snapshot.
        m_lastWindow.clear();
        m_lastWindow.reserve(m_sections.size());
        for (auto& [name, s] : m_sections) {
            const double frames = std::max<double>(s.windowFrames, 1.0);
            const double totalMs = s.windowNs / 1.0e6;
            Sample sm;
            sm.name           = name;
            sm.avgMsPerFrame  = totalMs / frames;
            sm.lastFrameMs    = totalMs;          // = "ms in this window"
            sm.lastFrameCalls = s.windowCalls;
            m_lastWindow.push_back(std::move(sm));

            // Reset window counters
            s.windowNs     = 0;
            s.windowCalls  = 0;
            s.windowFrames = 0;
        }
        std::sort(m_lastWindow.begin(), m_lastWindow.end(),
                  [](const Sample& a, const Sample& b) {
                      return a.avgMsPerFrame > b.avgMsPerFrame;
                  });
        m_windowStart = now;
    }
}

} // namespace v2v::utils
