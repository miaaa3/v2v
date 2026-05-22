#pragma once

#include "simulation/Vehicle.h"

#include <cstddef>
#include <vector>

namespace v2v::simulation {

/**
 * Flat container of Vehicle records.
 *
 * Vehicles are stored in a contiguous std::vector for iteration speed (the
 * step loop touches every vehicle every frame). The pool never reorders or
 * removes vehicles after spawn — vehicle indices double as stable handles
 * for the lifetime of the simulation.
 */
class VehiclePool {
public:
    VehiclePool() = default;

    void reserve(std::size_t n) { m_vehicles.reserve(n); }
    void clear() { m_vehicles.clear(); }

    VehicleId add(Vehicle v) {
        v.id = static_cast<VehicleId>(m_vehicles.size());
        m_vehicles.push_back(std::move(v));
        return m_vehicles.back().id;
    }

    std::size_t       size()       const { return m_vehicles.size(); }
    bool              empty()      const { return m_vehicles.empty(); }

    Vehicle&          operator[](std::size_t i)       { return m_vehicles[i]; }
    const Vehicle&    operator[](std::size_t i) const { return m_vehicles[i]; }

    auto              begin()       { return m_vehicles.begin(); }
    auto              end()         { return m_vehicles.end();   }
    auto              begin() const { return m_vehicles.begin(); }
    auto              end()   const { return m_vehicles.end();   }

    const std::vector<Vehicle>& vehicles() const { return m_vehicles; }
    std::vector<Vehicle>&       vehicles()       { return m_vehicles; }

private:
    std::vector<Vehicle> m_vehicles;
};

} // namespace v2v::simulation
