#include "gdlvl.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <exception>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <slic.hpp>

#include <difficulty.hpp>
#include <pathfinder.hpp>
#include <robustness.hpp>

namespace pathfinder_cli {
namespace {

// Passive data bound to the command line schema.
struct CliArgs {
  std::optional<std::string_view> input;
  std::optional<std::string_view> output_pos;
  std::optional<std::string_view> output_opt;
  bool inspect = false;
  bool difficulty = false;
  bool help = false;

  // slic discovers the schema through these exact member names.
  static constexpr std::string_view Description =
      "Generate a .gdr2 macro by pathfinding a .gdlvl level";
  static constexpr std::tuple Options = {
      slic::Option{"--output", "-o", &CliArgs::output_opt,
                   "Output .gdr2 file (defaults to the input path)"},
      slic::Option{"--inspect", "-i", &CliArgs::inspect,
                   "Print the decoded level string and exit"},
      slic::Option{"--difficulty", "-d", &CliArgs::difficulty,
                   "Print a 0-48 tier estimate"},
      slic::Option{"--help", "-h", &CliArgs::help,
                   "Show this help message"},
      slic::Arg{"INPUT", &CliArgs::input, "Input .gdlvl file"},
      slic::Arg{"OUTPUT", &CliArgs::output_pos, "Output .gdr2 file"},
  };
};

constinit std::atomic_bool g_stop{false};

// Fixed seed so repeated difficulty measurements are comparable.
constexpr uint32_t kDifficultySeed = 0xC0FFEEu;
constexpr int64_t kDifficultyMaxFrames = 100000000;

// An empty error means the macro was written successfully.
struct WriteResult {
  std::string error;
};

void HandleInterrupt(int) {
  g_stop.store(true);
}

void InstallSignalHandlers() {
  std::signal(SIGINT, HandleInterrupt);
#ifdef SIGTERM
  std::signal(SIGTERM, HandleInterrupt);
#endif
}

// Uses an explicit output when given, otherwise derives it from the input.
std::string ResolveOutputPath(CliArgs const& args) {
  if (args.output_opt.has_value() && !args.output_opt->empty()) {
    return std::string(*args.output_opt);
  }
  if (args.output_pos.has_value() && !args.output_pos->empty()) {
    return std::string(*args.output_pos);
  }
  std::string output(*args.input);
  size_t slash = output.find_last_of("/\\");
  size_t dot = output.find_last_of('.');
  if (dot != std::string::npos &&
      (slash == std::string::npos || dot > slash)) {
    output.erase(dot);
  }
  return output + ".gdr2";
}

WriteResult WriteMacro(std::string const& path,
                       std::vector<uint8_t> const& macro) {
  WriteResult result;
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    result.error = "could not open " + path + " for writing";
    return result;
  }
  file.write(reinterpret_cast<char const*>(macro.data()),
             static_cast<std::streamsize>(macro.size()));
  if (!file) {
    result.error = "failed to write " + path;
  }
  return result;
}

void ReportProgress(double progress) {
  std::cerr << "\rPathfinding... " << progress << "%" << std::flush;
}

// Writes the tier estimate, derived features, and raw telemetry to stdout.
void PrintDifficultyReport(PathfindReport const& report) {
  DifficultyAssessment assessment = AssessDifficulty(report.effort);
  DifficultyFeatures features = ExtractFeatures(report.effort);
  std::cout << "Tier: " << assessment.score << " / " << kMaxDifficulty << '\n';
  std::cout << "Raw tier: " << assessment.raw_score << '\n';
  std::cout << "Confidence: " << assessment.confidence << '\n';
  std::cout << "Sections:";
  for (double score : assessment.section_scores) {
    std::cout << ' ' << score;
  }
  std::cout << '\n';
  std::cout << "Features:\n";
  std::cout << "  effort_per_progress: " << features.effort_per_progress
            << '\n';
  std::cout << "  attempts_per_step: " << features.attempts_per_step << '\n';
  std::cout << "  failure_rate: " << features.failure_rate << '\n';
  std::cout << "  reset_rate: " << features.reset_rate << '\n';
  std::cout << "  max_rollback: " << features.max_rollback << '\n';
  std::cout << "  rejection_rate: " << features.rejection_rate << '\n';
  std::cout << "  dead_rate: " << features.dead_rate << '\n';
  std::cout << "  stall_rate: " << features.stall_rate << '\n';
  std::cout << "  luck: " << features.luck << '\n';
  std::cout << "  hardest_section: " << features.hardest_section << '\n';
  std::cout << "Search effort:\n";
  std::cout << "  completed: "
            << (report.effort.completed ? "yes" : "no") << '\n';
  std::cout << "  steps: " << report.effort.samples.size() << '\n';
  std::cout << "  candidate attempts: "
            << report.effort.total_candidate_attempts << '\n';
  std::cout << "  simulated frames: "
            << report.effort.total_simulated_frames << '\n';
  std::cout << "  committed frames: "
            << report.effort.committed_frames_total << '\n';
  std::cout << "  dead candidates: " << report.effort.dead_candidates << '\n';
  std::cout << "  stalled candidates: "
            << report.effort.stalled_candidates << '\n';
  std::cout << "  rejected candidates: "
            << report.effort.rejected_candidates << '\n';
  std::cout << "  failures: " << report.effort.failure_count << '\n';
  std::cout << "  max rollback: " << report.effort.max_rollback << '\n';
  std::cout << "  resets: " << report.effort.reset_count << '\n';
}

// Fixed seed so repeated robustness measurements are comparable.
constexpr uint32_t kRobustnessSeed = 0x5EEDC0DEu;
constexpr int64_t kRobustnessMaxFrames = 100000000;

// Runs the click-tolerance scan and noisy-player pass on the committed
// inputs and prints a robustness summary to stdout.
void PrintRobustnessReport(std::string const& lvl_string,
                           PathfindReport const& report) {
  RobustnessOptions options;
  options.rng_seed = kRobustnessSeed;
  options.max_simulated_frames = kRobustnessMaxFrames;
  RobustnessReport robustness = AnalyzeRobustness(lvl_string, report.effort,
                                                  options, g_stop);
  std::cout << "Robustness:\n";
  std::cout << "  confidence: " << robustness.confidence << '\n';
  std::cout << "  median tolerance: "
            << robustness.features.median_tolerance << " frames\n";
  std::cout << "  min tolerance: " << robustness.features.min_tolerance
            << " frames\n";
  std::cout << "  tight clicks: " << robustness.features.tight_click_rate
            << '\n';
  std::cout << "  noisy survival: " << robustness.features.survival << '\n';
  std::cout << "  section survival:";
  for (double rate : robustness.features.section_survival) {
    std::cout << ' ' << rate;
  }
  std::cout << '\n';
  // Printing every click would flood the output; show the tightest ones.
  std::vector<ClickTolerance> tight = robustness.clicks;
  std::sort(tight.begin(), tight.end(), [](ClickTolerance const& a,
                                           ClickTolerance const& b) {
    return std::min(a.early, a.late) < std::min(b.early, b.late);
  });
  constexpr size_t kMaxTightClicks = 10;
  if (tight.size() > kMaxTightClicks) {
    tight.resize(kMaxTightClicks);
  }
  if (!tight.empty()) {
    std::cout << "  tightest clicks (frame: press early..late, release "
                 "early..late):\n";
    for (ClickTolerance const& click : tight) {
      std::cout << "    " << click.frame << ": " << click.early << ".."
                << click.late << ", " << click.release_early << ".."
                << click.release_late << '\n';
    }
  }
}

int RunPathfinder(CliArgs const& args) {
  InstallSignalHandlers();
  LoadResult load = LoadLevelFile(std::string(*args.input));
  if (!load.error.empty()) {
    std::cerr << "Error: " << load.error << '\n';
    return 1;
  }
  std::cerr << "Loaded \"" << load.level.name << "\" ("
            << load.level.level_string.size() << " bytes of level data)\n";
  if (args.inspect) {
    std::cout << load.level.level_string << '\n';
    return 0;
  }

  std::vector<uint8_t> macro;
  // The core predates this guide and may throw; contain it here.
  try {
    if (args.difficulty) {
      PathfindOptions options{load.level.name};
      options.rng_seed = kDifficultySeed;
      options.max_simulated_frames = kDifficultyMaxFrames;
      PathfindReport report = PathfindWithEffort(
          load.level.level_string, g_stop, ReportProgress, options);
      PrintDifficultyReport(report);
      PrintRobustnessReport(load.level.level_string, report);
      macro = std::move(report.macro);
    } else {
      macro = pathfind(load.level.level_string, g_stop, ReportProgress,
                       PathfindOptions{load.level.name});
    }
  } catch (std::exception const& e) {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
  if (g_stop.load()) {
    std::cerr << "Stopped early, exporting best result so far.\n";
  }
  if (macro.empty()) {
    std::cerr << "Warning: the pathfinder produced an empty macro.\n";
  }

  std::string output = ResolveOutputPath(args);
  WriteResult write = WriteMacro(output, macro);
  if (!write.error.empty()) {
    std::cerr << "Error: " << write.error << '\n';
    return 1;
  }
  std::cerr << "Wrote " << macro.size() << " bytes to " << output << '\n';
  return 0;
}

}  // namespace
}  // namespace pathfinder_cli

int main(int argc, char** argv) {
  slic::ArgParser<pathfinder_cli::CliArgs> parser(argc, argv);
  auto result = parser.parse();
  if (!result) {
    result.print();
    parser.printHelp();
    return 1;
  }
  pathfinder_cli::CliArgs const& args = parser.result();
  if (args.help) {
    parser.printHelp();
    return 0;
  }
  if (!args.input.has_value()) {
    std::cerr << "Error: missing INPUT\n";
    parser.printHelp();
    return 1;
  }
  return pathfinder_cli::RunPathfinder(args);
}
