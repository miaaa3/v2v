# V2V Simulator

Real-time C++/OpenGL simulator for Vehicle-to-Vehicle (V2V) communication on an OpenStreetMap road network. Target: 2000+ vehicles, 60 FPS, dynamic communication graph.

## Stack

C++20, OpenGL 3.3 Core, GLFW, GLAD (glad2), GLM, Dear ImGui, stb_image. CMake + FetchContent.

Required on the host:

- **Python 3 + jinja2** (`pip install jinja2`) — used at configure time by the GLAD generator
- **libcurl** — used by `TileLoader` to fetch OSM raster tiles
  - macOS: shipped with the system, nothing to install
  - Linux (Debian/Ubuntu): `sudo apt install libcurl4-openssl-dev`
  - Windows: install via vcpkg (`vcpkg install curl`)

## Build

```bash
cmake -B build -S .
cmake --build build -j
./build/bin/V2V
```

First configure takes a few minutes (deps are cloned). Subsequent builds are incremental.

## Layout

```
src/
  graphics/   Window, Renderer, Camera2D, Shader
  simulation/ Vehicle, SimulationEngine, PathFinder      (step 5)
  map/        MapProjection, TileCache, TileLoader       (step 3)
  graph/      RoadGraph, OSMParser                       (step 4)
  ui/         ImGui panels                               (step 8)
  utils/      Logging, math helpers
shaders/      GLSL programs (basic.vert / basic.frag for now)
```

## Controls

- Right-click drag — pan
- Mouse wheel       — zoom (anchored on cursor)
- R                 — reset camera (graph bounds if loaded, else Casablanca z=13)
- G / M / V / L     — toggle road graph / map tiles / vehicles / V2V links
- SPACE             — pause / resume simulation
- + / -             — time scale x2 / ÷2 (0.0625x .. 16x)
- 0                 — time scale 1x
- F1                — toggle diagnostic overlay (stdout)
- ESC               — quit

ImGui panels are also docked on the left (Controls, Layers, Simulation, V2V
network, Performance). Keyboard / mouse input is suppressed while the cursor
is over a panel.

## Running with vehicles

```bash
./build/bin/V2V data/casablanca.osm           # default 1000 vehicles
./build/bin/V2V data/casablanca.osm 2000      # explicit count
```

Vehicles spawn on random graph nodes with random destinations. Each vehicle
runs A* (`PathFinder`) to its target — the engine budgets a fixed number of
A* searches per step (default 32) to keep frame times bounded, and falls back
to random walks at intersections when no path is queued yet.

## Loading a road graph

Pass an OSM XML file as a CLI argument:

```bash
./build/bin/V2V data/casablanca.osm
```

Fetch a city extract from the Overpass API (replace the bbox to taste —
`min_lat, min_lon, max_lat, max_lon`):

```bash
mkdir -p data
curl -G 'https://overpass-api.de/api/interpreter' \
  --data-urlencode 'data=[out:xml][timeout:120];
    (way["highway"~"motorway|trunk|primary|secondary|tertiary|residential|service|unclassified"](33.50,-7.75,33.65,-7.50););
    out body; >; out skel qt;' \
  -o data/casablanca.osm
```

The parser keeps only routable `highway=*` ways and inserts both directions
for non-oneway streets. Roundabouts (`junction=roundabout`) are treated as
oneway. Speeds from the `maxspeed` tag (km/h or mph) override the per-type
defaults (motorway 130, primary 80, residential 30, …).

## Notes on OSM tile usage

The default `TileLoader` URL template is `https://tile.openstreetmap.org/{z}/{x}/{y}.png`
(CARTO's basemaps, used earlier, now require an API key),
which is the public OSM tile server. Their [usage policy](https://operations.osmfoundation.org/policies/tiles/)
requires a valid User-Agent (we set one), reasonable rate-limiting, and is
unsuitable for production load — swap in a self-hosted server or a commercial
provider (Mapbox, MapTiler…) once you start hammering it.
