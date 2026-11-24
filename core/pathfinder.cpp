#include "pathfinder.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <set>
#include <utility>
#include <vector>

#include <gdr/gdr.hpp>

#include <Level.hpp>

namespace {

class Replay2 : public gdr::Replay<Replay2, gdr::Input<"">> {
 public:
  Replay2(std::string const& author, int version) : Replay(author, version) {}
};

struct Level2 : public Level {
  bool press = false;
  float highest_y = 0.0f;
  using Level::Level;

  explicit Level2(std::string const& lvl_string) : Level(lvl_string) {
    for (auto const& section : sections) {
      for (auto const& object : section) {
        highest_y = std::max(highest_y, object->pos.y);
      }
    }
  }
};

constexpr int kIterations = 300;
constexpr int kCandidateClicks = 30;
constexpr int kClickWindow = 1000;
constexpr int kBigAdvance = 500;

bool IsLevelEnd(Level2& lvl) {
  return lvl.latestState().pos.x >= lvl.length;
}

int TryInputs(Level2& lvl, std::set<uint16_t> inputs, SearchEffort* effort) {
  int frame = lvl.currentFrame();
  bool press_before = lvl.press;
  int simulated_frames = 0;

  while (!inputs.empty() && !lvl.gameStates.back().dead) {
    auto current = static_cast<uint16_t>(lvl.currentFrame());
    if (inputs.contains(current)) {
      lvl.press = !lvl.press;
      inputs.erase(current);
    }
    lvl.runFrame(lvl.press);
    ++simulated_frames;
  }

  int final_frame = lvl.currentFrame();
  bool dead = lvl.gameStates.back().dead;
  // Preserved from the original implementation: both bounds use pos.y.
  float last_x = lvl.latestState().pos.y;
  float last_y = lvl.latestState().pos.y;

  lvl.rollback(frame);
  lvl.press = press_before;

  bool rejected = last_x < lvl.length && (last_y > 1300 || last_y < 0);
  if (effort != nullptr) {
    effort->total_simulated_frames += simulated_frames;
    if (dead) {
      ++effort->dead_candidates;
    }
    if (rejected) {
      ++effort->rejected_candidates;
    }
  }
  return rejected ? 0 : final_frame;
}

std::vector<uint8_t> PathfindImpl(std::string const& lvl_string,
                                  std::atomic_bool& stop,
                                  std::function<void(double)> const& callback,
                                  PathfindOptions const& options,
                                  SearchEffort* effort) {
  Level2 lvl(lvl_string);

  std::random_device rd;
  std::mt19937 rng(options.rng_seed.has_value() ? *options.rng_seed : rd());
  std::uniform_int_distribution<int> dist(0, kClickWindow - 1);

  int true_best = 0;
  int fail = 1;
  int num_away = 1000;
  bool stopped = false;

  Level2 lvl_best = lvl;

  while (!IsLevelEnd(lvl)) {
    int frame = lvl.currentFrame();
    float start_x = lvl.latestState().pos.x;
    int simulated_before =
        effort != nullptr ? effort->total_simulated_frames : 0;
    int dead_before = effort != nullptr ? effort->dead_candidates : 0;
    int stalled_before = effort != nullptr ? effort->stalled_candidates : 0;

    std::set<uint16_t> best_inputs;
    int best_frame = frame;
    int attempts = 0;
    std::vector<int> reaches;
    reaches.reserve(kIterations);

    for (int i = 0; i < kIterations; ++i) {
      std::set<uint16_t> inputs;
      for (int j = 0; j < kCandidateClicks; ++j) {
        inputs.insert(static_cast<uint16_t>(frame + dist(rng)));
      }
      int reach = TryInputs(lvl, inputs, effort);
      ++attempts;
      if (effort != nullptr && reach <= frame) {
        ++effort->stalled_candidates;
      }
      // best_frame is an absolute frame; store deltas for the median too.
      reaches.push_back(reach - frame);
      if (reach > best_frame) {
        best_frame = reach;
        best_inputs = inputs;
        if (best_frame - frame > kBigAdvance && fail < 1000) {
          break;
        }
      }
    }

    int median_reach = 0;
    if (!reaches.empty()) {
      size_t middle = reaches.size() / 2;
      std::nth_element(reaches.begin(), reaches.begin() + middle,
                       reaches.end());
      median_reach = reaches[middle];
    }

    bool failed = best_frame == frame;
    bool did_reset = false;
    int rollback_frames = 0;
    int committed_frames = 0;

    if (failed) {
      int target = std::max(std::max(frame - fail, true_best - num_away), 1);
      rollback_frames = std::max(0, frame - target);
      lvl.rollback(target);
      fail += 5;

      if (fail > num_away + 1000) {
        num_away += 1000;
        fail = 1;
        if (num_away > 10000) {
          num_away = 1000;
          true_best = 0;
          lvl.rollback(1);
          rollback_frames = std::max(rollback_frames, frame - 1);
          did_reset = true;
        }
      } else if (fail > 100) {
        fail += 50;
      }
    } else {
      int limit =
          best_frame -
          static_cast<int>(static_cast<double>(best_frame - frame) / 1.5);
      for (int i = frame; i < limit; ++i) {
        if (best_inputs.contains(static_cast<uint16_t>(i))) {
          lvl.press = !lvl.press;
        }
        lvl.runFrame(lvl.press);
      }
      committed_frames = lvl.currentFrame() - frame;
    }

    if (effort != nullptr) {
      SearchEffortSample sample;
      sample.start_frame = frame;
      sample.start_x = start_x;
      sample.committed_frames = committed_frames;
      sample.candidate_attempts = attempts;
      sample.simulated_frames =
          effort->total_simulated_frames - simulated_before;
      sample.dead_candidates = effort->dead_candidates - dead_before;
      sample.stalled_candidates = effort->stalled_candidates - stalled_before;
      sample.best_reach = best_frame - frame;
      sample.median_reach = median_reach;
      sample.rollback_frames = rollback_frames;
      sample.failed = failed;
      effort->samples.push_back(sample);
      effort->total_candidate_attempts += attempts;
      effort->committed_frames_total += committed_frames;
      if (failed) {
        ++effort->failure_count;
      }
      if (did_reset) {
        ++effort->reset_count;
      }
      effort->max_rollback = std::max(effort->max_rollback, rollback_frames);
    }

    if (lvl.currentFrame() > true_best) {
      true_best = lvl.currentFrame();
      fail = 0;
      num_away = 1000;
    }
    if (lvl.currentFrame() > lvl_best.currentFrame()) {
      lvl_best = lvl;
    }
    if (callback) {
      callback(std::min((lvl.latestState().pos.x / lvl.length) * 100.0f,
                        100.0f));
    }
    if (stop) {
      stopped = true;
      break;
    }
    if (options.max_simulated_frames.has_value() && effort != nullptr &&
        effort->total_simulated_frames >= *options.max_simulated_frames) {
      stopped = true;
      break;
    }
  }

  if (effort != nullptr) {
    effort->final_frame = lvl.currentFrame();
    effort->final_x = lvl.latestState().pos.x;
    effort->completed = !stopped && IsLevelEnd(lvl);
    // Same extraction as the macro export below: frames where the button
    // is held on the committed replay. A press toggled at iteration i takes
    // effect during the sim of frame i, but the state pushed after that
    // frame is numbered i+1, so held frames are the state frame minus one.
    effort->committed.initial_press = lvl_best.gameStates.front().button;
    for (auto const& state : lvl_best.gameStates) {
      if (state.frame > 1 && state.button) {
        effort->committed.held_frames.insert(
            static_cast<int>(state.frame) - 1);
      }
    }
  }

  Replay2 output(options.author, options.author_version);
  output.levelInfo.name = options.level_name;
  for (auto const& state : lvl_best.gameStates) {
    if (state.frame > 1 && state.button != state.prevPlayer().button) {
      output.inputs.push_back(gdr::Input(state.frame, 1, false, state.button));
    }
  }
  return output.exportData().unwrapOr({});
}

}  // namespace

std::vector<uint8_t> pathfind(std::string const& lvl_string,
                              std::atomic_bool& stop,
                              std::function<void(double)> callback,
                              PathfindOptions const& options) {
  return PathfindImpl(lvl_string, stop, callback, options, nullptr);
}

PathfindReport PathfindWithEffort(std::string const& lvl_string,
                                  std::atomic_bool& stop,
                                  std::function<void(double)> callback,
                                  PathfindOptions const& options) {
  PathfindReport report;
  std::vector<uint8_t> macro =
      PathfindImpl(lvl_string, stop, callback, options, &report.effort);
  report.macro = std::move(macro);
  return report;
}

std::vector<uint8_t> pathfind(std::string const& lvl_string,
                              std::atomic_bool& stop,
                              std::function<void(double)> callback) {
  return pathfind(lvl_string, stop, std::move(callback), PathfindOptions{});
}
