#pragma once

#include "graph/RoadGraph.h"

#include <string>

namespace v2v::graph {

/**
 * OSM XML reader.
 *
 * Parses an `.osm` file (the XML output of the Overpass API or
 * `osmosis`) and populates a RoadGraph. Only `<way>` elements with a routable
 * `highway=*` tag are kept; everything else is ignored. Two-way streets are
 * inserted as a pair of directed edges; oneway streets only the forward one.
 *
 * The graph is *appended* to (it is not cleared first), so you can merge
 * multiple files.
 */
class OSMParser {
public:
    struct Stats {
        std::size_t osmNodesRead    = 0;
        std::size_t osmWaysRead     = 0;
        std::size_t waysAccepted    = 0;
        std::size_t nodesInserted   = 0;
        std::size_t edgesInserted   = 0;
        std::string error;            ///< empty on success
        bool ok() const { return error.empty(); }
    };

    /// Parse from a file path on disk.
    static Stats parseFile(const std::string& path, RoadGraph& outGraph);

    /// Parse from an in-memory XML buffer (e.g. fetched from Overpass).
    static Stats parseBuffer(const char* data, std::size_t size, RoadGraph& outGraph);
};

} // namespace v2v::graph
