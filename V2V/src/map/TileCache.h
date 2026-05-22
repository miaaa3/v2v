#pragma once

#include "map/TileKey.h"
#include "map/TileLoader.h"

#include <cstdint>
#include <list>
#include <unordered_map>

namespace v2v::map {

/// State of a tile slot in the cache.
enum class TileStatus : std::uint8_t {
    Pending,   ///< queued for download
    Ready,     ///< texture uploaded and renderable
    Failed,    ///< download or decode failed; will not retry until evicted
};

struct Tile {
    TileStatus    status  = TileStatus::Pending;
    std::uint32_t texture = 0;  // GL texture id (0 unless Ready)
};

/**
 * LRU cache of GL-uploaded OSM tiles.
 *
 * Public flow:
 *   - `getOrRequest(key, loader)`: returns a pointer to the cache entry.
 *     If the tile is missing, it is created in `Pending` state and a load
 *     request is dispatched to `loader`. Touching a tile updates its LRU
 *     position.
 *   - `pump(loader)`: must be called each frame from the GL thread. Drains
 *     newly completed downloads from the loader and uploads their pixels to
 *     textures (status flips to `Ready`).
 *
 * Capacity is enforced after `pump()` and inside `getOrRequest()`: when the
 * cache exceeds `maxTiles`, the least-recently-used Ready/Failed entry is
 * evicted (Pending entries are never evicted to avoid churn).
 */
class TileCache {
public:
    explicit TileCache(std::size_t maxTiles = 512);
    ~TileCache();

    TileCache(const TileCache&)            = delete;
    TileCache& operator=(const TileCache&) = delete;

    /// Get the tile entry, requesting a load if absent. Updates LRU.
    Tile* getOrRequest(TileKey key, TileLoader& loader);

    /// Drain `loader` and upload finished pixels as GL textures.
    /// Must be called from the GL thread once per frame.
    void pump(TileLoader& loader);

    /// Wipe the cache, deleting all GL textures.
    void clear();

    std::size_t size()     const { return m_map.size(); }
    std::size_t capacity() const { return m_maxTiles;   }

private:
    struct Entry {
        Tile                          tile;
        std::list<TileKey>::iterator  lruIt;
    };

    std::size_t                          m_maxTiles;
    std::list<TileKey>                   m_lru;   // front = most recent
    std::unordered_map<TileKey, Entry>   m_map;

    void touch(TileKey key);            // assumes key exists
    void evictIfNeeded();
    void deleteTexture(std::uint32_t&) const;
};

} // namespace v2v::map
