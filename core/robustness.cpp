#include "robustness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <set>
#include <utility>

#include <Level.hpp>
#include <Player.hpp>

namespace {

// Replays the level from the start. Mirrors the search's TryInputs loop: the
// desired press state is the press set's membership of the current frame,
// passed to runFrame directly, since preCollision latches button changes
// itself. Returns the frame the player reached and sets reach_x to the x
// position at that frame. The button state is never cached across iterations:
// runFrame grows the state vector, so a cached Player& would dangle.
int Replay(Level& lvl, bool initial_press, std::set<int> const& held_frames,
           int final_frame, float* reach_x) {
  lvl.rollback(1);
  lvl.gameStates.back().button = initial_press;

  int frame = lvl.currentFrame();
  while (frame < final_frame && !lvl.gameStates.back().dead) {
    lvl.runFrame(held_frames.contains(frame));
    frame = lvl.currentFrame();
  }
  *reach_x = lvl.latestState().pos.x;
  return frame;
}

// Shifts one hold's press and release independently and returns whether the
// run still completes. press_shift moves the whole hold; release_shift
// extends (positive) or shortens (negative) it at the release end.
bool Survives(Level const& reference, std::set<int> held_frames, int start,
              int end, int press_shift, int release_shift, int final_frame) {
  int new_start = std::max(1, start + press_shift);
  int new_end = std::max(new_start, end + press_shift + release_shift);
  for (int frame = start; frame <= end; ++frame) {
    held_frames.erase(frame);
  }
  for (int frame = new_start; frame <= new_end; ++frame) {
    held_frames.insert(frame);
  }
  Level lvl = reference;
  float reach_x = 0.0f;
  int frame = Replay(lvl, reference.gameStates.front().button, held_frames,
                     final_frame, &reach_x);
  return frame >= final_frame && !lvl.gameStates.back().dead;
}

// Largest k in [0, max_shift] for which shifting in the given direction
// still survives. direction < 0 probes earlier presses, > 0 later ones;
// release > 0 probes the release end instead of the press end. Adds the
// frames each probe simulated to simulated.
int Scan(Level const& reference, std::set<int> const& held_frames, int start,
         int end, int direction, bool release, int max_shift,
         int final_frame, int64_t* simulated) {
  int slack = 0;
  bool survive = true;
  int shift = 0;
  while (survive && slack < max_shift) {
    ++shift;
    survive = Survives(reference, held_frames, start, end,
                       release ? 0 : direction * shift,
                       release ? direction * shift : 0, final_frame);
    *simulated += final_frame;
    if (survive) {
      ++slack;
    }
  }
  return slack;
}

}  // namespace

RobustnessReport AnalyzeRobustness(std::string const& lvl_string,
                                   SearchEffort const& effort,
                                   RobustnessOptions const& options,
                                   std::atomic_bool const& stop) {
  RobustnessReport report;
  report.features.section_survival.assign(kRobustnessSections, 0.0);
  if (!effort.completed || effort.final_frame <= 0 ||
      effort.committed.held_frames.empty()) {
    report.confidence = "low - no completed run to replay";
    return report;
  }

  Level reference(lvl_string);
  std::set<int> held_frames = effort.committed.held_frames;
  int final_frame = effort.final_frame;
  double span = static_cast<double>(reference.length);

  int64_t simulated = 0;
  bool budget_hit = false;

  // Tolerance scan: probe each hold with the four natural human mistakes —
  // press early, press late, release early, release late — until the run
  // stops surviving. Holds are contiguous frame ranges, so scanning each
  // frame of a hold separately would re-test the same shift many times;
  // scanning the range once per direction keeps the cost proportional to
  // the hold length.
  std::vector<int> tolerances;
  std::set<int> processed;
  for (int frame : held_frames) {
    if (processed.contains(frame) || stop.load()) {
      continue;
    }
    // Extend along the contiguous run of frames belonging to this hold.
    int end = frame;
    while (held_frames.contains(end)) {
      processed.insert(end);
      ++end;
    }
    int start = frame;
    ClickTolerance tolerance;
    tolerance.frame = start;
    tolerance.early = Scan(reference, held_frames, start, end, -1, false,
                           options.max_shift, final_frame, &simulated);
    tolerance.late = Scan(reference, held_frames, start, end, +1, false,
                          options.max_shift, final_frame, &simulated);
    tolerance.release_early = Scan(reference, held_frames, start, end, -1,
                                   true, options.max_shift, final_frame,
                                   &simulated);
    tolerance.release_late = Scan(reference, held_frames, start, end, +1,
                                  true, options.max_shift, final_frame,
                                  &simulated);
    tolerances.push_back(std::min({tolerance.early, tolerance.late,
                                   tolerance.release_early,
                                   tolerance.release_late}));
    report.clicks.push_back(tolerance);
    if (options.max_simulated_frames.has_value() &&
        simulated >= *options.max_simulated_frames) {
      budget_hit = true;
      break;
    }
  }

  if (!tolerances.empty()) {
    std::vector<int> sorted = tolerances;
    size_t middle = sorted.size() / 2;
    std::nth_element(sorted.begin(), sorted.begin() + middle, sorted.end());
    report.features.median_tolerance = sorted[middle];
    report.features.min_tolerance =
        *std::min_element(sorted.begin(), sorted.end());
    int tight = 0;
    for (int tolerance : tolerances) {
      if (tolerance <= kTightClickFrames) {
        ++tight;
      }
    }
    report.features.tight_click_rate =
        static_cast<double>(tight) / static_cast<double>(tolerances.size());
  }

  // Noisy player: jitter every hold by Gaussian noise and estimate how often
  // the run survives, overall and per section of the level.
  if (options.noise_trials > 0 && !stop.load()) {
    // Contiguous runs of held frames. Jittering frames independently would
    // fragment holds, releasing the button mid-flight; a human mistimes the
    // click itself, so the whole hold shifts.
    std::vector<std::pair<int, int>> holds;
    for (int frame : held_frames) {
      if (!holds.empty() && holds.back().second + 1 == frame) {
        holds.back().second = frame;
      } else {
        holds.push_back({frame, frame});
      }
    }

    std::mt19937 rng(options.rng_seed.value_or(0x5EEDC0DEu));
    std::normal_distribution<double> jitter(0.0, options.noise_sigma);

    int survivors = 0;
    // Reach fraction of every trial, for the per-section survival rates.
    std::vector<double> reach_fractions;
    reach_fractions.reserve(options.noise_trials);
    for (int trial = 0; trial < options.noise_trials && !stop.load();
         ++trial) {
      std::set<int> noisy;
      for (auto const& [start, end] : holds) {
        int shift = static_cast<int>(std::lround(jitter(rng)));
        for (int frame = start; frame <= end; ++frame) {
          noisy.insert(std::max(1, frame + shift));
        }
      }
      Level lvl = reference;
      float reach_x = 0.0f;
      int frame = Replay(lvl, reference.gameStates.front().button, noisy,
                         final_frame, &reach_x);
      simulated += frame;
      bool survived = frame >= final_frame && !lvl.gameStates.back().dead;
      if (survived) {
        ++survivors;
      }
      reach_fractions.push_back(
          span > 0.0 ? std::min(1.0, static_cast<double>(reach_x) / span)
                     : 0.0);
      if (options.max_simulated_frames.has_value() &&
          simulated >= *options.max_simulated_frames) {
        budget_hit = true;
        break;
      }
    }

    int trials = static_cast<int>(reach_fractions.size());
    if (trials > 0) {
      report.features.survival =
          static_cast<double>(survivors) / static_cast<double>(trials);
      // section_survival[i] is the share of trials that got past section i.
      for (double fraction : reach_fractions) {
        for (int i = 0; i < kRobustnessSections; ++i) {
          if (fraction >
              static_cast<double>(i + 1) / kRobustnessSections) {
            ++report.features.section_survival[i];
          }
        }
      }
      for (double& rate : report.features.section_survival) {
        rate /= static_cast<double>(trials);
      }
    }
  }

  if (stop.load()) {
    report.confidence = "low - stopped early";
  } else if (budget_hit) {
    report.confidence = "low - frame budget hit before completion";
  }
  return report;
}
