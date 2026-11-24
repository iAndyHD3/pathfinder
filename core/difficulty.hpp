#ifndef PATHFINDER_CORE_DIFFICULTY_H_
#define PATHFINDER_CORE_DIFFICULTY_H_

#include <set>
#include <string>
#include <utility>
#include <vector>

// Upper bound of the tier scale. Non-demon levels occupy 0-9; demon tiers
// start at 10 (website tier 1) and end at 48 (website tier 39).
constexpr double kMaxDifficulty = 48.0;



// Passive record of one committed step of the pathfinding search.
struct SearchEffortSample {
  int start_frame = 0;
  float start_x = 0.0f;
  int committed_frames = 0;
  int candidate_attempts = 0;
  int simulated_frames = 0;
  int dead_candidates = 0;
  int stalled_candidates = 0;
  int best_reach = 0;
  int median_reach = 0;
  int rollback_frames = 0;
  bool failed = false;
};

// Frames at which the committed replay holds the button, plus the state
// before the first frame. Derived from the same game states the macro
// export reads, so an analyzer can replay the committed path.
struct CommittedInputs {
  // Frames where the button is held; checked per frame during replay,
  // matching how the sim consumes its own candidate input sets.
  std::set<int> held_frames;
  // Button state before the first frame.
  bool initial_press = false;
};

// Passive record of all search effort spent on a level.
struct SearchEffort {
  std::vector<SearchEffortSample> samples;
  // Input list of the committed replay. Empty on incomplete runs.
  CommittedInputs committed;
  int total_simulated_frames = 0;
  int total_candidate_attempts = 0;
  int committed_frames_total = 0;
  int dead_candidates = 0;
  int stalled_candidates = 0;
  int rejected_candidates = 0;
  int failure_count = 0;
  int max_rollback = 0;
  int reset_count = 0;
  int final_frame = 0;
  float final_x = 0.0f;
  bool completed = false;
};

// Passive set of normalized features derived from search telemetry.
struct DifficultyFeatures {
  double effort_per_progress = 0.0;
  double attempts_per_step = 0.0;
  double failure_rate = 0.0;
  double reset_rate = 0.0;
  double max_rollback = 0.0;
  double rejection_rate = 0.0;
  double dead_rate = 0.0;
  double stall_rate = 0.0;
  double luck = 0.0;
  double hardest_section = 0.0;
  std::vector<double> section_scores;
};

// Coefficients of a linear tier fit over normalized search features.
struct TierModel {
  double intercept = 0.0;
  double attempts = 0.0;
  double rollback = 0.0;
  double luck = 0.0;
};

// Two regimes: a fit over non-demon reference levels, and a fit over website
// demon tiers. The latter is shifted by 9 on output so that tier 10 is easy
// demon. The two are blended near the demon boundary.
struct DifficultyWeights {
  TierModel easy = {0.98, 18.83, 20.70, 0.0};
  TierModel demon = {-33.1, 35.98, 7.90, 21.08};
};

// Passive result of scoring a search effort record.
struct DifficultyAssessment {
  double score = 0.0;
  double raw_score = 0.0;
  std::vector<double> section_scores;
  std::string confidence;
};

// Derives normalized features and a per-section profile from raw telemetry.
DifficultyFeatures ExtractFeatures(SearchEffort const& effort);

// Maps derived features onto the tier scale.
DifficultyAssessment AssessFeatures(
    DifficultyFeatures const& features,
    DifficultyWeights const& weights = DifficultyWeights{});

// Convenience wrapper around ExtractFeatures and AssessFeatures.
DifficultyAssessment AssessDifficulty(
    SearchEffort const& effort,
    DifficultyWeights const& weights = DifficultyWeights{});

#endif  // PATHFINDER_CORE_DIFFICULTY_H_
