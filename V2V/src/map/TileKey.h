#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace v2v::map {

/// OSM slippy-map tile coordinate: integer (zoom, x, y).
///   - z in [0, 19]   (OSM tile zoom levels)
///   - x in [0, 2^z)  (column, west→east)
///   - y in [0, 2^z)  (row,    north→south)
struct TileKey {
    int z = 0;
    int x = 0;
    int y = 0;

    constexpr bool operator==(const TileKey& other) const noexcept {
        return z == other.z && x == other.x && y == other.y;
    }
    constexpr bool operator!=(const TileKey& other) const noexcept {
        return !(*this == other);
    }
};

} // namespace v2v::map

namespace std {

template <>
struct hash<v2v::map::TileKey> {
    std::size_t operator()(const v2v::map::TileKey& k) const noexcept {
        // Pack into a 64-bit integer: z (5 bits) | x (29) | y (29). Plenty of
        // room — OSM only goes to z=19 so x,y are < 2^19.
        std::uint64_t h =
            (static_cast<std::uint64_t>(k.z) << 58) ^
            (static_cast<std::uint64_t>(k.x) << 29) ^
             static_cast<std::uint64_t>(k.y);
        // Mix (splitmix64 finalizer) to avoid clustering.
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return static_cast<std::size_t>(h);
    }
};

} // namespace std
