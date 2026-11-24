#ifndef PATHFINDER_CLI_GDLVL_H_
#define PATHFINDER_CLI_GDLVL_H_

#include <string>

namespace pathfinder_cli {

// Passive result of loading a level file.
struct LevelFile {
  std::string level_string;
  std::string name;
};

// Passive result of LoadLevelFile. The error is empty on success.
struct LoadResult {
  LevelFile level;
  std::string error;
};

// Loads a .gdlvl file: a gzipped plist, a plain plist, or a raw level string.
// The error is empty when the level was loaded successfully.
LoadResult LoadLevelFile(std::string const& path);

}  // namespace pathfinder_cli

#endif  // PATHFINDER_CLI_GDLVL_H_
