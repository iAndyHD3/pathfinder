#ifndef PATHFINDER_CORE_PATHFINDER_H_
#define PATHFINDER_CORE_PATHFINDER_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "difficulty.hpp"

// Passive metadata embedded in the exported macro.
struct PathfindOptions {
  std::string level_name;
  std::string author = "Pathfinder";
  int author_version = 1;
  // Fixes the RNG seed so difficulty measurements are reproducible.
  std::optional<uint32_t> rng_seed;
  // Stops the search after this many simulated frames, exporting partial
  // telemetry. Machine-independent, unlike a wall-clock budget.
  std::optional<int64_t> max_simulated_frames;
};

// Passive result of a pathfinding run with search telemetry.
struct PathfindReport {
  std::vector<uint8_t> macro;
  SearchEffort effort;
};

// Finds a macro for the given plain level string. The stop flag can be set
// from another thread to export the best result so far. The callback receives
// progress in percent. Returns the bytes of a .gdr2 replay.
std::vector<uint8_t> pathfind(std::string const& lvl_string,
                              std::atomic_bool& stop,
                              std::function<void(double)> callback);

// Same as above, with metadata for the exported replay.
std::vector<uint8_t> pathfind(std::string const& lvl_string,
                              std::atomic_bool& stop,
                              std::function<void(double)> callback,
                              PathfindOptions const& options);

// Same as above, but also records how much search effort the run required.
PathfindReport PathfindWithEffort(std::string const& lvl_string,
                                  std::atomic_bool& stop,
                                  std::function<void(double)> callback,
                                  PathfindOptions const& options);

#endif  // PATHFINDER_CORE_PATHFINDER_H_
