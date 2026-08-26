#ifndef SCOTTY_LINUX_QML_TRAY_APP_VERSION_COMPARE_H_
#define SCOTTY_LINUX_QML_TRAY_APP_VERSION_COMPARE_H_

#include <string>

// Parses and compares Scotty release tags so the update checker can decide
// whether a GitHub release is newer than the running build.
//
// Tag shapes seen in the wild on github.com/scottydev40/Scotty (all handled):
//   v0.1.0-beta1.7   v0.1.0-beta1   v0.1.0   v0.1-beta-2   v0.0.9-beta-1
// A leading 'v' is optional. A missing patch means .0.
//
// Ordering follows semver's prerelease rule: a beta precedes its release,
// so 0.1.0-beta1.7 < 0.1.0. Betas order by beta number, then beta-minor
// (beta1 == beta1.0).
namespace scotty {

struct ParsedVersion {
  bool valid = false;
  int major = 0;
  int minor = 0;
  int patch = 0;
  bool is_beta = false;
  int beta = 0;        // beta series number, e.g. 1 in beta1.7
  int beta_minor = 0;  // sub-number, e.g. 7 in beta1.7 (0 when absent)
};

// Parses a release tag. Returns {valid=false} if it is not a recognizable
// version (empty, "nightly", etc.).
ParsedVersion ParseTag(const std::string& tag);

// Returns <0 if a<b, 0 if equal, >0 if a>b. An invalid version sorts below
// any valid one (and two invalids compare equal).
int CompareVersions(const ParsedVersion& a, const ParsedVersion& b);

// Convenience for the update path: is `candidate` a strictly newer release
// than `current`? An unparseable candidate is never newer (fail safe).
bool IsNewer(const std::string& candidate, const std::string& current);

}  // namespace scotty

#endif  // SCOTTY_LINUX_QML_TRAY_APP_VERSION_COMPARE_H_
