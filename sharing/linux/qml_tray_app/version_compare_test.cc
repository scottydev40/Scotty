// Unit tests for the Scotty release-tag parser + comparator.
//
// Pure std C++ (no Qt) so it builds and runs fast standalone:
//   g++ -std=c++20 version_compare_test.cc version_compare.cc -o /tmp/vt && /tmp/vt
//
// Covers the real, historically-inconsistent tag shapes seen on
// github.com/scottydev40/Scotty releases.

#include "version_compare.h"

#include <cassert>
#include <cstdio>
#include <string>

using scotty::ParsedVersion;
using scotty::CompareVersions;
using scotty::ParseTag;
using scotty::IsNewer;

static int g_failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      ++g_failures;                                                    \
    }                                                                  \
  } while (0)

static void parses_full_beta_tag() {
  ParsedVersion v = ParseTag("v0.1.0-beta1.7");
  CHECK(v.valid);
  CHECK(v.major == 0);
  CHECK(v.minor == 1);
  CHECK(v.patch == 0);
  CHECK(v.is_beta);
  CHECK(v.beta == 1);
  CHECK(v.beta_minor == 7);
}

static void parses_stable_tag() {
  ParsedVersion v = ParseTag("v0.1.0");
  CHECK(v.valid);
  CHECK(v.major == 0 && v.minor == 1 && v.patch == 0);
  CHECK(!v.is_beta);
}

static void parses_missing_patch() {
  // Older scheme: "v0.1" means 0.1.0.
  ParsedVersion v = ParseTag("v0.1-beta-2");
  CHECK(v.valid);
  CHECK(v.major == 0 && v.minor == 1 && v.patch == 0);
  CHECK(v.is_beta && v.beta == 2 && v.beta_minor == 0);
}

static void parses_dash_beta_scheme() {
  // Older scheme: "-beta-1" (dash before the number).
  ParsedVersion a = ParseTag("v0.0.9-beta-1");
  CHECK(a.valid && a.major == 0 && a.minor == 0 && a.patch == 9);
  CHECK(a.is_beta && a.beta == 1 && a.beta_minor == 0);
}

static void tolerates_missing_v_prefix() {
  ParsedVersion v = ParseTag("0.1.0-beta1.7");
  CHECK(v.valid && v.major == 0 && v.minor == 1 && v.patch == 0 && v.beta == 1);
}

static void rejects_garbage() {
  CHECK(!ParseTag("").valid);
  CHECK(!ParseTag("nightly").valid);
}

static void stable_outranks_beta_of_same_xyz() {
  // Semver: 0.1.0 > 0.1.0-beta1.7  (a prerelease precedes its release).
  CHECK(CompareVersions(ParseTag("v0.1.0"), ParseTag("v0.1.0-beta1.7")) > 0);
  CHECK(CompareVersions(ParseTag("v0.1.0-beta1.7"), ParseTag("v0.1.0")) < 0);
}

static void higher_patch_wins() {
  CHECK(CompareVersions(ParseTag("v0.1.1"), ParseTag("v0.1.0")) > 0);
  CHECK(CompareVersions(ParseTag("v0.2.0"), ParseTag("v0.1.9")) > 0);
  CHECK(CompareVersions(ParseTag("v1.0.0"), ParseTag("v0.9.9")) > 0);
}

static void beta_ordering_within_same_xyz() {
  CHECK(CompareVersions(ParseTag("v0.1.0-beta1.7"),
                        ParseTag("v0.1.0-beta1.6")) > 0);
  CHECK(CompareVersions(ParseTag("v0.1.0-beta2"),
                        ParseTag("v0.1.0-beta1.9")) > 0);
  CHECK(CompareVersions(ParseTag("v0.1.0-beta1"),
                        ParseTag("v0.1.0-beta1.5")) < 0);  // beta1 == beta1.0
}

static void equal_tags_compare_equal() {
  CHECK(CompareVersions(ParseTag("v0.1.0-beta1.7"),
                        ParseTag("0.1.0-beta1.7")) == 0);
}

static void is_newer_helper() {
  // The real question the app asks: is the remote tag newer than mine?
  CHECK(IsNewer(/*candidate=*/"v0.1.0-beta1.8", /*current=*/"v0.1.0-beta1.7"));
  CHECK(!IsNewer("v0.1.0-beta1.7", "v0.1.0-beta1.7"));
  CHECK(!IsNewer("v0.1.0-beta1.6", "v0.1.0-beta1.7"));
  // An unparseable candidate is never "newer" (fail safe).
  CHECK(!IsNewer("nightly", "v0.1.0-beta1.7"));
}

int main() {
  parses_full_beta_tag();
  parses_stable_tag();
  parses_missing_patch();
  parses_dash_beta_scheme();
  tolerates_missing_v_prefix();
  rejects_garbage();
  stable_outranks_beta_of_same_xyz();
  higher_patch_wins();
  beta_ordering_within_same_xyz();
  equal_tags_compare_equal();
  is_newer_helper();

  if (g_failures == 0) {
    std::printf("all version_compare tests passed\n");
    return 0;
  }
  std::printf("%d version_compare assertion(s) failed\n", g_failures);
  return 1;
}
