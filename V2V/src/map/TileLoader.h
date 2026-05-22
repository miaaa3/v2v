#pragma once

#include "map/TileKey.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace v2v::map {

/**
 * Asynchronous tile downloader.
 *
 * A small pool of worker threads pulls TileKey requests from a queue, HTTP-
 * GETs the corresponding OSM raster tile, decodes the PNG to RGBA, and pushes
 * a `LoadedTile` to the result queue. The main thread (which owns the GL
 * context) drains the result queue with `tryPop` and uploads to a texture.
 *
 * Network access uses libcurl; the User-Agent follows OSM tile usage policy.
 */
class TileLoader {
public:
    struct LoadedTile {
        TileKey                key{};
        std::vector<std::uint8_t> rgba;    // 4 bytes per pixel, row-major, top-down
        int                    width  = 0;
        int                    height = 0;
        bool                   success = false;
    };

    explicit TileLoader(int numWorkers = 4,
                        std::string urlTemplate =
                            "https://cartodb-basemaps-a.global.ssl.fastly.net/rastertiles/voyager/{z}/{x}/{y}.png",
                        std::string userAgent =
                            "V2V-Simulator/0.1 (https://github.com/meryem-belmouaddine/v2v)");
    ~TileLoader();

    TileLoader(const TileLoader&)            = delete;
    TileLoader& operator=(const TileLoader&) = delete;

    /// Enqueue a load (no-op if already queued or in flight).
    void request(TileKey key);

    /// Main-thread drain. Returns false if no result is ready.
    bool tryPop(LoadedTile& out);

    /// Approximate counts (for diagnostics).
    std::size_t pendingCount() const;
    std::size_t completedCount() const;

private:
    void workerLoop();
    bool fetchAndDecode(TileKey key, LoadedTile& out) const;
    std::string formatUrl(TileKey key) const;

    const std::string       m_urlTemplate;
    const std::string       m_userAgent;
    std::atomic<bool>       m_stop{false};

    // Request side
    mutable std::mutex           m_reqMutex;
    std::condition_variable      m_reqCv;
    std::deque<TileKey>          m_requests;
    std::unordered_set<TileKey>  m_inFlight;

    // Result side
    mutable std::mutex           m_resMutex;
    std::deque<LoadedTile>       m_results;

    std::vector<std::thread>     m_workers;
};

} // namespace v2v::map
