#include "map/MapProjection.h"

#include <algorithm>
#include <cmath>

namespace v2v::map {

namespace {
constexpr double kPi    = 3.14159265358979323846;
constexpr double kDeg2R = kPi / 180.0;
constexpr double kRad2D = 180.0 / kPi;
} // namespace

glm::dvec2 MapProjection::latLonToWorld(double latDeg, double lonDeg)
{
    const double clampedLat = std::clamp(latDeg, MIN_LAT_DEG, MAX_LAT_DEG);
    const double latRad     = clampedLat * kDeg2R;
    const double lonRad     = lonDeg     * kDeg2R;

    const double x = EARTH_RADIUS_M * lonRad;
    const double y = EARTH_RADIUS_M * std::log(std::tan(kPi / 4.0 + latRad / 2.0));
    return { x, y };
}

glm::dvec2 MapProjection::worldToLatLon(glm::dvec2 world)
{
    const double lonRad = world.x / EARTH_RADIUS_M;
    const double latRad = 2.0 * std::atan(std::exp(world.y / EARTH_RADIUS_M)) - kPi / 2.0;
    return { latRad * kRad2D, lonRad * kRad2D };
}

double MapProjection::tileSizeMeters(int z)
{
    const int safeZ = std::clamp(z, 0, MAX_ZOOM);
    return WORLD_SIZE_M / static_cast<double>(1ULL << safeZ);
}

int MapProjection::tilesPerAxis(int z)
{
    const int safeZ = std::clamp(z, 0, MAX_ZOOM);
    return 1 << safeZ;
}

glm::dvec2 MapProjection::tileTopLeftWorld(TileKey key)
{
    const double ts = tileSizeMeters(key.z);
    const double x  = -EARTH_HALF_M + static_cast<double>(key.x) * ts;
    const double y  =  EARTH_HALF_M - static_cast<double>(key.y) * ts; // tile y goes south
    return { x, y };
}

glm::dvec4 MapProjection::tileWorldRect(TileKey key)
{
    const double ts        = tileSizeMeters(key.z);
    const glm::dvec2 topLeft = tileTopLeftWorld(key);
    const double left   = topLeft.x;
    const double right  = topLeft.x + ts;
    const double top    = topLeft.y;
    const double bottom = topLeft.y - ts;
    return { left, bottom, right, top };
}

int MapProjection::chooseZoomLevel(double pixelsPerMeter)
{
    if (pixelsPerMeter <= 0.0) return 0;

    // tileSize_world * pixelsPerMeter ≈ TILE_PIXELS
    // → tileSize_world = TILE_PIXELS / pixelsPerMeter
    // tileSize_world = WORLD_SIZE_M / 2^z
    // → 2^z = WORLD_SIZE_M * pixelsPerMeter / TILE_PIXELS
    const double twoPowZ = WORLD_SIZE_M * pixelsPerMeter / static_cast<double>(TILE_PIXELS);
    const double zFloat  = std::log2(std::max(twoPowZ, 1.0));
    return std::clamp(static_cast<int>(std::round(zFloat)), 0, MAX_ZOOM);
}

} // namespace v2v::map
