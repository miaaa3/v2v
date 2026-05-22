#pragma once

#include <glm/glm.hpp>

#include <algorithm>

namespace v2v::vnet {

/**
 * Tiny radio link model.
 *
 * Quadratic falloff between full strength (1.0) at distance 0 and zero at
 * `range`. This is *not* a physics-grade path-loss model — it captures the
 * "closer is stronger" intuition for visualization. Étape 8 / future work
 * can swap it out for log-distance or Friis-style models without touching
 * V2VNetwork or LinkRenderer.
 */
struct RadioModel {
    /// Signal strength in [0, 1]. Returns 0 if `distance >= range`.
    static float strength(float distance, float range)
    {
        if (range <= 0.0f || distance >= range) return 0.0f;
        const float t = distance / range;
        return 1.0f - t * t;
    }

    /// Color along a red → yellow → green ramp keyed by strength.
    /// Alpha is also boosted with strength so weak links fade out.
    static glm::vec4 colorFor(float strength)
    {
        strength = std::clamp(strength, 0.0f, 1.0f);
        glm::vec3 rgb;
        if (strength >= 0.5f) {
            const float t = (strength - 0.5f) * 2.0f;   // 0..1
            rgb = { 1.0f - t, 1.0f, 0.2f * t };
        } else {
            const float t = strength * 2.0f;            // 0..1
            rgb = { 1.0f, t, 0.0f };
        }
        const float alpha = 0.25f + 0.55f * strength;
        return { rgb.r, rgb.g, rgb.b, alpha };
    }
};

} // namespace v2v::vnet
