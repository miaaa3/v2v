#include "graph/OSMParser.h"
#include "map/MapProjection.h"
#include "utils/Log.h"

#include <pugixml.hpp>

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace v2v::graph {

namespace {

// Parse OSM "maxspeed" tag. Examples: "50", "50 mph", "RO:urban".
// Returns 0 if unparseable (caller will fall back to type default).
float parseMaxspeed(std::string_view raw)
{
    if (raw.empty()) return 0.0f;

    // Take leading number.
    char buf[32]{};
    std::size_t i = 0;
    while (i < raw.size() && i < sizeof(buf) - 1 &&
           (std::isdigit(static_cast<unsigned char>(raw[i])) || raw[i] == '.')) {
        buf[i] = raw[i];
        ++i;
    }
    if (i == 0) return 0.0f;
    buf[i]      = '\0';
    float speed = static_cast<float>(std::atof(buf));
    if (speed <= 0.0f) return 0.0f;

    // Check for mph suffix.
    while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    if (raw.size() - i >= 3 &&
        (raw[i] == 'm' || raw[i] == 'M') &&
        (raw[i + 1] == 'p' || raw[i + 1] == 'P') &&
        (raw[i + 2] == 'h' || raw[i + 2] == 'H'))
    {
        speed *= 1.609344f;
    }
    return speed;
}

// Parse a way's tags into RoadType, oneway, maxspeed. Returns false if the way
// is not a routable highway.
bool extractWayAttrs(const pugi::xml_node& way,
                     RoadType& outType, bool& outOneway, float& outSpeed)
{
    outType   = RoadType::Other;
    outOneway = false;
    outSpeed  = 0.0f;

    bool hasHighway = false;
    for (auto tag : way.children("tag")) {
        const std::string_view k = tag.attribute("k").value();
        const std::string_view v = tag.attribute("v").value();

        if (k == "highway") {
            outType    = parseRoadType(v);
            hasHighway = true;
        }
        else if (k == "oneway") {
            outOneway = (v == "yes" || v == "true" || v == "1");
        }
        else if (k == "maxspeed") {
            outSpeed = parseMaxspeed(v);
        }
        else if (k == "junction") {
            // Roundabouts are implicitly oneway in OSM convention.
            if (v == "roundabout" || v == "circular") outOneway = true;
        }
    }

    if (!hasHighway || outType == RoadType::Other) return false;
    return true;
}

OSMParser::Stats parseInternal(pugi::xml_document& doc, RoadGraph& outGraph)
{
    OSMParser::Stats st;

    auto osmRoot = doc.child("osm");
    if (!osmRoot) {
        st.error = "Missing <osm> root element";
        return st;
    }

    // 1. Index all <node> elements by OSM id.
    struct OsmNode { double lat; double lon; };
    std::unordered_map<std::int64_t, OsmNode> osmNodes;
    osmNodes.reserve(1024);

    for (auto n : osmRoot.children("node")) {
        const std::int64_t id  = n.attribute("id").as_llong();
        const double       lat = n.attribute("lat").as_double();
        const double       lon = n.attribute("lon").as_double();
        osmNodes.emplace(id, OsmNode{ lat, lon });
        ++st.osmNodesRead;
    }

    // 2. Iterate <way> elements; for each routable one, add nodes + edges.
    std::unordered_map<std::int64_t, NodeIndex> osmIdToNode;
    osmIdToNode.reserve(osmNodes.size() / 2);

    auto getOrAddNode = [&](std::int64_t osmId) -> NodeIndex {
        auto it = osmIdToNode.find(osmId);
        if (it != osmIdToNode.end()) return it->second;

        auto nIt = osmNodes.find(osmId);
        if (nIt == osmNodes.end()) return INVALID_NODE;

        const glm::dvec2 latLon{ nIt->second.lat, nIt->second.lon };
        const glm::dvec2 world  = map::MapProjection::latLonToWorld(latLon.x, latLon.y);
        const NodeIndex  idx    = outGraph.addNode(latLon, world);
        osmIdToNode.emplace(osmId, idx);
        ++st.nodesInserted;
        return idx;
    };

    std::vector<std::int64_t> wayNodeIds;

    for (auto way : osmRoot.children("way")) {
        ++st.osmWaysRead;

        RoadType type;
        bool     oneway;
        float    speed;
        if (!extractWayAttrs(way, type, oneway, speed)) continue;

        wayNodeIds.clear();
        for (auto nd : way.children("nd")) {
            wayNodeIds.push_back(nd.attribute("ref").as_llong());
        }
        if (wayNodeIds.size() < 2) continue;

        ++st.waysAccepted;

        NodeIndex prevIdx = getOrAddNode(wayNodeIds.front());
        for (std::size_t i = 1; i < wayNodeIds.size(); ++i) {
            const NodeIndex curIdx = getOrAddNode(wayNodeIds[i]);
            if (prevIdx == INVALID_NODE || curIdx == INVALID_NODE) {
                prevIdx = curIdx;
                continue;
            }
            if (prevIdx == curIdx) {
                prevIdx = curIdx;
                continue;
            }

            outGraph.addEdge(prevIdx, curIdx, type, oneway, speed);
            ++st.edgesInserted;
            if (!oneway) {
                outGraph.addEdge(curIdx, prevIdx, type, false, speed);
                ++st.edgesInserted;
            }
            prevIdx = curIdx;
        }
    }

    return st;
}

} // namespace

OSMParser::Stats OSMParser::parseFile(const std::string& path, RoadGraph& outGraph)
{
    Stats st;
    pugi::xml_document doc;
    const auto res = doc.load_file(path.c_str(),
                                   pugi::parse_default | pugi::parse_ws_pcdata_single);
    if (!res) {
        st.error = std::string("pugixml load_file failed: ") + res.description() +
                   " (offset " + std::to_string(res.offset) + ")";
        return st;
    }
    auto out = parseInternal(doc, outGraph);
    V2V_LOG_INFO("OSM parse: nodes=%zu/%zu, ways=%zu/%zu accepted, edges=%zu",
                 out.nodesInserted, out.osmNodesRead,
                 out.waysAccepted,  out.osmWaysRead,
                 out.edgesInserted);
    return out;
}

OSMParser::Stats OSMParser::parseBuffer(const char* data, std::size_t size, RoadGraph& outGraph)
{
    Stats st;
    pugi::xml_document doc;
    const auto res = doc.load_buffer(data, size,
                                     pugi::parse_default | pugi::parse_ws_pcdata_single);
    if (!res) {
        st.error = std::string("pugixml load_buffer failed: ") + res.description();
        return st;
    }
    return parseInternal(doc, outGraph);
}

} // namespace v2v::graph
