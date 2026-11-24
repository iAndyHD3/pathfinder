#include "difficulty.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int kSectionCount = 10;

// Raw values that map to a full pressure of 1.0.
constexpr double kAttemptsReference = 300.0;
constexpr double kRollbackReference = 2000.0;
constexpr double kLuckReference = 3.0;
constexpr double kSectionEffortReference = 600.0;
constexpr double kSectionFailureReference = 0.25;
constexpr double kSectionStallReference = 0.50;
constexpr double kDemonOffset = 9.0;
constexpr double kBlendStart = 1.0;
constexpr double kBlendWidth = 2.0;

double Evaluate(TierModel const& model, double attempts, double rollback,
                double luck) {
  return model.intercept + model.attempts * attempts +
         model.rollback * rollback + model.luck * luck;
}

double Pressure(double value, double reference) {
  if (reference <= 0.0) {
    return 0.0;
  }
  return std::clamp(value / reference, 0.0, 1.0);
}

double Rate(int count, int total) {
  return total > 0 ? static_cast<double>(count) / static_cast<double>(total)
                   : 0.0;
}

double RoundToOneDecimal(double value) {
  return std::round(value * 10.0) / 10.0;
}

}  // namespace

DifficultyFeatures ExtractFeatures(SearchEffort const& effort) {
  DifficultyFeatures features;
  int steps = static_cast<int>(effort.samples.size());
  if (steps == 0) {
    return features;
  }

  int committed_total = 0;
  for (SearchEffortSample const& sample : effort.samples) {
    committed_total += sample.committed_frames;
  }

  features.effort_per_progress = committed_total > 0
      ? static_cast<double>(effort.total_simulated_frames) /
            static_cast<double>(committed_total)
      : 0.0;
  features.attempts_per_step =
      static_cast<double>(effort.total_candidate_attempts) /
      static_cast<double>(steps);
  features.failure_rate = Rate(effort.failure_count, steps);
  features.reset_rate = Rate(effort.reset_count, steps);
  features.max_rollback = static_cast<double>(effort.max_rollback);
  features.rejection_rate =
      Rate(effort.rejected_candidates, effort.total_candidate_attempts);
  features.dead_rate = Rate(effort.dead_candidates,
                            effort.total_candidate_attempts);
  features.stall_rate = Rate(effort.stalled_candidates,
                             effort.total_candidate_attempts);

  // A large best/median reach gap means progress depended on getting lucky
  // with the random candidate sample rather than being easy to find.
  double luck = 0.0;
  int lucky_steps = 0;
  for (SearchEffortSample const& sample : effort.samples) {
    if (sample.median_reach <= 0) {
      continue;
    }
    luck += static_cast<double>(sample.best_reach) /
            static_cast<double>(sample.median_reach);
    ++lucky_steps;
  }
  features.luck = lucky_steps > 0 ? luck / static_cast<double>(lucky_steps)
                                  : 0.0;

  features.section_scores.assign(kSectionCount, 0.0);
  double span = static_cast<double>(effort.final_x);
  if (span > 0.0) {
    std::vector<int> simulated(kSectionCount, 0);
    std::vector<int> committed(kSectionCount, 0);
    std::vector<int> failures(kSectionCount, 0);
    std::vector<int> stalled(kSectionCount, 0);
    std::vector<int> section_steps(kSectionCount, 0);
    for (SearchEffortSample const& sample : effort.samples) {
      int index = static_cast<int>(static_cast<double>(sample.start_x) / span *
                                   kSectionCount);
      index = std::clamp(index, 0, kSectionCount - 1);
      simulated[index] += sample.simulated_frames;
      committed[index] += sample.committed_frames;
      if (sample.failed) {
        ++failures[index];
      }
      stalled[index] += sample.stalled_candidates;
      ++section_steps[index];
    }
    for (int i = 0; i < kSectionCount; ++i) {
      if (section_steps[i] == 0) {
        continue;
      }
      double section_effort = 0.0;
      if (committed[i] > 0) {
        section_effort = static_cast<double>(simulated[i]) /
                         static_cast<double>(committed[i]);
      } else {
        // No progress at all in this section is maximal effort.
        section_effort = kSectionEffortReference;
      }
      double pressure =
          0.55 * Pressure(section_effort, kSectionEffortReference) +
          0.25 * Pressure(Rate(failures[i], section_steps[i]),
                          kSectionFailureReference) +
          0.20 * Pressure(Rate(stalled[i], effort.total_candidate_attempts),
                          kSectionStallReference);
      features.section_scores[i] =
          RoundToOneDecimal(std::clamp(pressure, 0.0, 1.0) * kMaxDifficulty);
    }
  }
  features.hardest_section = *std::max_element(
      features.section_scores.begin(), features.section_scores.end());
  return features;
}

DifficultyAssessment AssessFeatures(DifficultyFeatures const& features,
                                    DifficultyWeights const& weights) {
  DifficultyAssessment result;
  double attempts = Pressure(features.attempts_per_step, kAttemptsReference);
  double rollback = Pressure(features.max_rollback, kRollbackReference);
  double luck = Pressure(features.luck, kLuckReference);
  double easy_tier = Evaluate(weights.easy, attempts, rollback, luck);
  double demon_raw = Evaluate(weights.demon, attempts, rollback, luck);
  double blend =
      std::clamp((demon_raw - kBlendStart) / kBlendWidth, 0.0, 1.0);
  double tier = easy_tier * (1.0 - blend) + (demon_raw + kDemonOffset) * blend;
  result.raw_score = RoundToOneDecimal(demon_raw);
  result.score = RoundToOneDecimal(std::clamp(tier, 0.0, kMaxDifficulty));
  result.section_scores = features.section_scores;
  return result;
}

DifficultyAssessment AssessDifficulty(SearchEffort const& effort,
                                      DifficultyWeights const& weights) {
  DifficultyFeatures features = ExtractFeatures(effort);
  DifficultyAssessment result = AssessFeatures(features, weights);
  int steps = static_cast<int>(effort.samples.size());
  if (steps == 0) {
    result.confidence = "low - no search steps recorded";
  } else if (!effort.completed) {
    result.confidence = "low - incomplete run (frame budget or stop)";
  } else if (steps < 10) {
    result.confidence = "low - too few search steps";
  } else {
    result.confidence = "low - fitted on a small reference set";
  }
  return result;
}
