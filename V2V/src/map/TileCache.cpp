#include "map/TileCache.h"
#include "utils/Log.h"

#include <glad/gl.h>

namespace v2v::map {

TileCache::TileCache(std::size_t maxTiles)
    : m_maxTiles(maxTiles == 0 ? 1 : maxTiles)
{}

TileCache::~TileCache()
{
    clear();
}

void TileCache::clear()
{
    for (auto& [key, entry] : m_map) {
        deleteTexture(entry.tile.texture);
    }
    m_map.clear();
    m_lru.clear();
}

void TileCache::deleteTexture(std::uint32_t& tex) const
{
    if (tex) {
        glDeleteTextures(1, &tex);
        tex = 0;
    }
}

void TileCache::touch(TileKey key)
{
    auto it = m_map.find(key);
    if (it == m_map.end()) return;
    m_lru.erase(it->second.lruIt);
    m_lru.push_front(key);
    it->second.lruIt = m_lru.begin();
}

Tile* TileCache::getOrRequest(TileKey key, TileLoader& loader)
{
    auto it = m_map.find(key);
    if (it != m_map.end()) {
        touch(key);
        return &it->second.tile;
    }

    // Brand new entry — request load.
    m_lru.push_front(key);
    Entry e;
    e.tile.status = TileStatus::Pending;
    e.lruIt       = m_lru.begin();
    auto [ins, ok] = m_map.emplace(key, std::move(e));

    loader.request(key);
    evictIfNeeded();
    return &ins->second.tile;
}

void TileCache::pump(TileLoader& loader)
{
    TileLoader::LoadedTile lt;
    int drained = 0;
    while (loader.tryPop(lt)) {
        auto it = m_map.find(lt.key);
        if (it == m_map.end()) {
            // The entry was evicted before its load completed — discard.
            continue;
        }
        Tile& tile = it->second.tile;
        if (!lt.success) {
            tile.status = TileStatus::Failed;
            continue;
        }

        // Upload as RGBA8.
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     lt.width, lt.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE,
                     lt.rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        tile.texture = tex;
        tile.status  = TileStatus::Ready;
        ++drained;
    }

    if (drained > 0) {
        evictIfNeeded();
    }
}

void TileCache::evictIfNeeded()
{
    while (m_map.size() > m_maxTiles) {
        // Walk LRU from the back to find an evictable entry (not Pending).
        auto rit = m_lru.rbegin();
        bool evicted = false;
        while (rit != m_lru.rend()) {
            auto mit = m_map.find(*rit);
            if (mit != m_map.end() && mit->second.tile.status != TileStatus::Pending) {
                deleteTexture(mit->second.tile.texture);
                m_lru.erase(std::next(rit).base());
                m_map.erase(mit);
                evicted = true;
                break;
            }
            ++rit;
        }
        if (!evicted) {
            // Every entry is Pending — wait for next pump rather than thrash.
            break;
        }
    }
}

} // namespace v2v::map
