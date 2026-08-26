#include "version_compare.h"

#include <cctype>
#include <cstddef>
#include <string>

namespace scotty {
namespace {

// Reads a run of decimal digits starting at `pos`, returns the value and
// advances `pos` past them. Returns false if there was no digit.
bool ReadInt(const std::string& s, std::size_t& pos, int& out) {
  if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) {
    return false;
  }
  int value = 0;
  while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
    value = value * 10 + (s[pos] - '0');
    ++pos;
  }
  out = value;
  return true;
}

// Ranks the prerelease component so a stable release outranks any beta of the
// same major.minor.patch (semver: 1.0.0-beta < 1.0.0). Stable => "infinity".
constexpr long kStableRank = 1L << 40;

long PrereleaseRank(const ParsedVersion& v) {
  if (!v.is_beta) return kStableRank;
  // beta number dominates beta-minor; keep them well-separated so beta2 always
  // outranks beta1.<anything>.
  return static_cast<long>(v.beta) * 100000L + v.beta_minor;
}

}  // namespace

ParsedVersion ParseTag(const std::string& tag) {
  ParsedVersion v;
  std::size_t pos = 0;

  // Optional leading 'v'.
  if (pos < tag.size() && (tag[pos] == 'v' || tag[pos] == 'V')) ++pos;

  // major (required).
  if (!ReadInt(tag, pos, v.major)) return v;  // invalid

  // .minor (optional -> 0).
  if (pos < tag.size() && tag[pos] == '.') {
    ++pos;
    if (!ReadInt(tag, pos, v.minor)) return v;  // "0." with no minor -> invalid
  }

  // .patch (optional -> 0).
  if (pos < tag.size() && tag[pos] == '.') {
    ++pos;
    if (!ReadInt(tag, pos, v.patch)) return v;
  }

  // Optional prerelease: "-beta" then a number, allowing an extra separator
  // ("-beta-1", "-beta1", "-beta1.7").
  if (pos < tag.size() && tag[pos] == '-') {
    std::size_t p = pos + 1;
    const std::string kBeta = "beta";
    if (tag.compare(p, kBeta.size(), kBeta) == 0) {
      p += kBeta.size();
      // optional separator between "beta" and its number
      if (p < tag.size() && (tag[p] == '-' || tag[p] == '.')) ++p;
      int n = 0;
      if (ReadInt(tag, p, n)) {
        v.is_beta = true;
        v.beta = n;
        // optional ".<minor>"
        if (p < tag.size() && tag[p] == '.') {
          ++p;
          int m = 0;
          if (ReadInt(tag, p, m)) v.beta_minor = m;
        }
      }
    }
    // Unrecognized suffixes are ignored; the x.y.z already parsed stands.
  }

  v.valid = true;
  return v;
}

int CompareVersions(const ParsedVersion& a, const ParsedVersion& b) {
  if (a.valid != b.valid) return a.valid ? 1 : -1;
  if (!a.valid && !b.valid) return 0;

  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;

  long ra = PrereleaseRank(a);
  long rb = PrereleaseRank(b);
  if (ra != rb) return ra < rb ? -1 : 1;
  return 0;
}

bool IsNewer(const std::string& candidate, const std::string& current) {
  ParsedVersion c = ParseTag(candidate);
  if (!c.valid) return false;  // fail safe: never treat garbage as an update
  return CompareVersions(c, ParseTag(current)) > 0;
}

}  // namespace scotty
