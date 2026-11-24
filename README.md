# Pathfinder
# Geometry Dash Physics Simulator

Full support up to 1.7, partial up to 1.9. This code is not currently licensed for redistribution. 

## Standalone CLI

The pathfinder core (`core/`) and the simulator (`gd-sim/`) have no Geode dependency, so the same
code can be used outside the game to turn a `.gdlvl` file into a `.gdr2` macro:

```sh
cmake -S . -B build -DPATHFINDER_BUILD_MOD=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
build/cli/pathfinder-cli level.gdlvl output.gdr2
```

`output.gdr2` is optional (also available as `-o/--output`) and defaults to the input path with a
`.gdr2` extension. Progress is printed to stderr; press Ctrl+C to stop early and export the best
result so far. `pathfinder-cli --inspect level.gdlvl` prints the decoded level string without
pathfinding; `--help` lists all options. Argument parsing uses [slic](https://github.com/prevter/slic).

`pathfinder-cli --difficulty level.gdlvl` runs the search with a fixed RNG seed and
prints a heuristic 0-48 tier estimate, a 10-section profile, derived features,
and the raw search-effort counters to stdout. Tiers 0-9 cover auto through
insane; tier 10 is easy demon and the demon tiers follow the community scale
shifted up by 9 (top tier 48). The search stops after 100 million simulated
frames so very hard levels cannot hang. The formula uses separate fits for
non-demon and demon tiers; deviations are expected, especially for older and
easier levels.

The CLI understands the usual GD `.gdlvl` layout (a base64+gzip compressed plist whose `k4` field
holds the base64+gzip level string), plain plists, and raw `1;...;` level strings.

Build switches: `PATHFINDER_BUILD_CLI` is on by default, `PATHFINDER_BUILD_MOD` is off by default
(pass `-DPATHFINDER_BUILD_MOD=ON` to build the mod). A CLI-only build needs no `GEODE_SDK`; it uses
the vendored [CPM.cmake](cmake/CPM.cmake) in `cmake/` to fetch its dependencies. The mod target
uses Geode's own CPM and still needs `GEODE_SDK`.

## Notes

1. Y positions are slightly off because in the real gd, they are offest by 105
