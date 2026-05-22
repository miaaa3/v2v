#pragma once

#include "map/TileKey.h"

#include <glm/glm.hpp>

namespace v2v::map {

/**
 * Web Mercator (EPSG:3857) projection helpers.
 *
 * World units throughout the simulator are EPSG:3857 *meters*:
 *   x ∈ [-EARTH_HALF, EARTH_HALF]      (west / east)
 *   y ∈ [-EARTH_HALF, EARTH_HALF]      (south / north, Y-up)
 *
 * Where EARTH_HALF = π · R, R = 6378137 m (WGS84 spherical radius).
 *
 * Latitude is clamped to ±85.05112878° (the Mercator limit). All angles are
 * in degrees on the public API; internally we convert to radians.
 */
class MapProjection {
public:
    static constexpr double EARTH_RADIUS_M = 6378137.0;
    static constexpr double EARTH_HALF_M   = 20037508.342789244; // π · R
    static constexpr double WORLD_SIZE_M   = 2.0 * EARTH_HALF_M;
    static constexpr double MIN_LAT_DEG    = -85.05112877980659;
    static constexpr double MAX_LAT_DEG    =  85.05112877980659;
    static constexpr int    TILE_PIXELS    = 256;
    static constexpr int    MAX_ZOOM       = 19;

    // ---- lat/lon ↔ world meters --------------------------------------------
    static glm::dvec2 latLonToWorld(double latDeg, double lonDeg);
    static glm::dvec2 worldToLatLon(glm::dvec2 world);

    // ---- tile ↔ world meters -----------------------------------------------
    /// World-meter size of a tile at zoom level z.
    static double tileSizeMeters(int z);

    /// World-meter rectangle covered by tile (z, x, y).
    /// Returns (left, bottom, right, top).
    static glm::dvec4 tileWorldRect(TileKey key);

    /// Top-left corner (in world meters) of tile (z, x, y).
    static glm::dvec2 tileTopLeftWorld(TileKey key);

    // ---- zoom selection ---------------------------------------------------
    /// Pick the OSM zoom level whose tiles render at approximately 256 screen
    /// pixels each, given the current camera `pixelsPerMeter` (Camera2D::zoom).
    /// Clamped to [0, MAX_ZOOM].
    static int chooseZoomLevel(double pixelsPerMeter);

    /// Number of tiles along an axis at zoom z: 2^z.
    static int tilesPerAxis(int z);
};

} // namespace v2v::map
