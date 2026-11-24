#ifndef PATHFINDER_CORE_ROBUSTNESS_H_
#define PATHFINDER_CORE_ROBUSTNESS_H_

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include "difficulty.hpp"

// Clicks with a tolerance of at most this many frames count as frame
// perfect. The sim runs at 240 fps, so one frame is about 4 ms.
constexpr int kTightClickFrames = 2;

// Sections map level progress onto 10 equally spaced buckets, matching the
// search-effort profile.
constexpr int kRobustnessSections = 10;

// Options for the robustness pass. All timing values are in sim frames.
struct RobustnessOptions {
  // Largest shift any single click is tested with, in frames.
  int max_shift = 12;
  // Standard deviation of the noisy player's click jitter, in frames.
  double noise_sigma = 2.0;
  // Number of noisy-player trials.
  int noise_trials = 128;
  // Machine-independent frame budget across both passes; none by default.
  std::optional<int64_t> max_simulated_frames;
  // Fixes the RNG seed so repeated measurements are comparable.
  std::optional<uint32_t> rng_seed;
};

// Per-click result of the jitter tolerance scan.
struct ClickTolerance {
  // Frame of the click's press within the macro.
  int frame = 0;
  // How many frames earlier this click can be pressed and still survive.
  int early = 0;
  // How many frames later this click can be pressed and still survive.
  int late = 0;
  // How many frames the hold can be shortened and still survive.
  int release_early = 0;
  // How many frames the hold can be extended and still survive.
  int release_late = 0;
};

// Passive record of robustness measurements over one solved macro.
struct RobustnessFeatures {
  // Median of per-click tolerance (min of early/late), in frames.
  double median_tolerance = 0.0;
  // Smallest per-click tolerance on the committed path, in frames.
  int min_tolerance = 0;
  // Share of clicks with tolerance <= kTightClickFrames.
  double tight_click_rate = 0.0;
  // Per-section survival rate of the noisy player (10 buckets, 0-1).
  std::vector<double> section_survival;
  // Overall survival rate of the noisy player (0-1).
  double survival = 0.0;
};

// Result of the robustness pass: derived features plus per-click detail.
struct RobustnessReport {
  RobustnessFeatures features;
  // Per-click tolerance scan, ordered by frame.
  std::vector<ClickTolerance> clicks;
  // Empty when the pass ran to completion.
  std::string confidence;
};

// Replays the committed inputs recorded by PathfindWithEffort under click
// jitter and measures how much timing slack each input has, plus how a noisy
// player fares. Derived passively from the search's telemetry; the search
// state itself is not touched. Requires a completed search, so plain
// pathfind runs cannot be analyzed. The level string must be the same one
// the search ran on, since the committed path is replayed from scratch.
RobustnessReport AnalyzeRobustness(std::string const& lvl_string,
                                   SearchEffort const& effort,
                                   RobustnessOptions const& options,
                                   std::atomic_bool const& stop);

#endif  // PATHFINDER_CORE_ROBUSTNESS_H_
