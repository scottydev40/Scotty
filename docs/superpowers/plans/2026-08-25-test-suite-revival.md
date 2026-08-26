# Test Suite Revival Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a curated subset of the fork's bazel test suite build and pass — covering both the connections-logic (on the `g3` fake platform) and Scotty's Linux bindings + sharing — and guard it with a `scripts/run-tests.sh` runner plus a GitHub Actions job.

**Architecture:** The fork was trimmed to build only the shipping `.so`; the test tree fails bazel *analysis* on unvendored Google-internal packages. We resolve only what the curated set needs (approach A: stub the trivial `buildenv` gating, vendor just `g3:types`, fix the protobuf-json ref) and choose targets that avoid webrtc/gloop/fuzz. A single script lists the targets; CI runs that script.

**Tech Stack:** Bazel (user-local at `~/.local/bin/bazel`), C++20, googletest, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-25-test-suite-revival-design.md`

## Global Constraints

- **Bazel invocation flags (verbatim, used everywhere):**
  `--check_visibility=false --spawn_strategy=standalone --cxxopt=-std=c++20 --host_cxxopt=-std=c++20 --@com_google_protobuf//bazel/toolchains:prefer_prebuilt_protoc=true --copt=-fvisibility=hidden --cxxopt=-fvisibility-inlines-hidden`
- **Bazel binary:** `/home/vardhv/.local/bin/bazel`
- **Repo root:** `/home/vardhv/Desktop/scotty`
- **Branch:** `test-suite-revival` (already created; commit incrementally so any task is revertable).
- **Phase 1 excludes** webrtc, gloop, and fuzz targets. Any curated candidate that cannot build without them is dropped from Phase 1 and recorded in the Phase-2 backlog — never vendor webrtc/gloop/fuzztest in this plan.
- **g3 vendoring** pins to the fork's last-synced upstream revision (the `ble_v2` sync of 2026-02-04) to avoid API drift.
- Every task ends green (`bazel build`/`test` succeeds for its deliverable) and is committed.

---

### Task 1: Dependency audit — finalize the buildable target list

**Files:**
- Create: `docs/superpowers/plans/2026-08-25-test-suite-revival-audit.md` (findings)

**Interfaces:**
- Produces: `CANDIDATES` (the confirmed list of test targets that build after the Task 2–4 fixes) — consumed by Tasks 5–8.

- [ ] **Step 1: List candidate targets and their missing transitive deps**

Run, for each candidate, a dep probe (analysis only, no compile):

```bash
cd /home/vardhv/Desktop/scotty
FLAGS="--check_visibility=false --spawn_strategy=standalone --cxxopt=-std=c++20 --host_cxxopt=-std=c++20 --@com_google_protobuf//bazel/toolchains:prefer_prebuilt_protoc=true --copt=-fvisibility=hidden --cxxopt=-fvisibility-inlines-hidden"
for t in \
  //connections/implementation:pcp_handler_test \
  //connections/implementation:pcp_manager_test \
  //connections/implementation:endpoint_channel_test \
  //connections/implementation:base_endpoint_channel_test \
  //connections/implementation:bwu_test \
  //connections/implementation/mediums:core_internal_mediums_test \
  //connections/implementation/mediums:bwu_handler_test \
  //connections/implementation/mediums/ble:ble_test \
  //connections/implementation/mediums/ble:ble_socket_test \
  //internal/platform/implementation/linux:impl_test \
  //internal/platform/implementation/linux/tests:linux_connections_test ; do
    echo "=== $t ==="
    /home/vardhv/.local/bin/bazel cquery $FLAGS "somepath($t, //third_party/webrtc/...)" 2>&1 | tail -2
    /home/vardhv/.local/bin/bazel build --nobuild $FLAGS "$t" 2>&1 | grep -E "no such package|Analysis of target|ERROR" | head -5
done
```

- [ ] **Step 2: Also enumerate sharing test targets**

Run:
```bash
grep -A1 "cc_test" sharing/BUILD | grep "name =" | head -40
```
Probe each with the same `bazel build --nobuild` loop.

- [ ] **Step 3: Record findings**

Write `docs/superpowers/plans/2026-08-25-test-suite-revival-audit.md` with three lists:
`BUILDS_NOW`, `NEEDS_FIX` (which of buildenv/g3-types/protobuf-json each needs),
`DEFER_PHASE2` (transitively needs webrtc/gloop/fuzz). This is `CANDIDATES`
(= BUILDS_NOW ∪ NEEDS_FIX) for later tasks.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-08-25-test-suite-revival-audit.md
git commit -m "docs(test): dependency audit of candidate test targets"
```

---

### Task 2: Neutralize the `buildenv/target:non_prod` gating

**Files:**
- Modify: `connections/implementation/analytics/BUILD` (and any other hits)

**Interfaces:**
- Consumes: nothing.
- Produces: the `//connections/implementation/analytics` package analyzes without the `buildenv` error.

Rationale: `compatible_with = ["//buildenv/target:non_prod"]` is Google-internal
prod/non-prod gating. Do **not** stub it as a constraint (a constraint the host
platform lacks would mark the target *incompatible* and silently skip dependent
tests). Remove the attribute instead.

- [ ] **Step 1: Find every usage**

Run:
```bash
cd /home/vardhv/Desktop/scotty
grep -rn 'compatible_with = \["//buildenv/target:non_prod"\]' --include=BUILD .
```
Expected: at least `connections/implementation/analytics/BUILD:79`.

- [ ] **Step 2: Verify it currently fails analysis**

Run:
```bash
/home/vardhv/.local/bin/bazel build --nobuild $FLAGS //connections/implementation/analytics:analytics 2>&1 | grep -i "buildenv"
```
Expected: `no such package 'buildenv/target'`.

- [ ] **Step 3: Remove the attribute**

For each hit, delete the line `compatible_with = ["//buildenv/target:non_prod"],`
(remove the whole attribute line, keeping the surrounding rule intact). Add a
one-line comment above the affected rule: `# fork-local: dropped //buildenv prod gating (test enablement)`.

- [ ] **Step 4: Verify analytics analyzes**

Run:
```bash
/home/vardhv/.local/bin/bazel build --nobuild $FLAGS //connections/implementation/analytics:analytics 2>&1 | tail -3
```
Expected: no `buildenv` error (may still error on other unvendored deps — those are handled in later tasks; there must be no `buildenv/target` line).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "build(test): drop //buildenv prod gating so analytics analyzes"
```

---

### Task 3: Vendor `//internal/platform/implementation/g3:types`

**Files:**
- Modify: `internal/platform/implementation/g3/BUILD`
- Create: vendored source(s) the `types` target needs (exact files determined here)

**Interfaces:**
- Consumes: nothing.
- Produces: `//internal/platform/implementation/g3:types` builds — consumed by the connections-logic tests.

- [ ] **Step 1: Confirm the gap**

Run:
```bash
cd /home/vardhv/Desktop/scotty
/home/vardhv/.local/bin/bazel query //internal/platform/implementation/g3:types 2>&1 | tail -3
grep -n "types" internal/platform/implementation/g3/BUILD
```
Expected: target `types` absent or referencing missing srcs.

- [ ] **Step 2: Fetch the upstream definition at the pinned revision**

Find the fork's last upstream sync commit for g3, then read upstream's target:
```bash
git log -1 --format=%H -- internal/platform/implementation/g3
# From github.com/google/nearby at the matching revision, read:
#   internal/platform/implementation/g3/BUILD  (the cc_library name = "types")
#   and the headers/sources it lists (e.g. *.h that define the g3 fake types).
```
Use `WebFetch` on the raw upstream files
(`https://raw.githubusercontent.com/google/nearby/<rev>/internal/platform/implementation/g3/BUILD`)
to read the exact `types` `cc_library` block and its `srcs`/`hdrs`/`deps`.

- [ ] **Step 3: Vendor the target + any missing source files**

Add the `types` `cc_library` block to `internal/platform/implementation/g3/BUILD`
verbatim from upstream. For each `hdrs`/`srcs` file it lists that is not already
present in the dir, create it with the upstream file's contents (same pinned rev).
Do not pull `deps` that route into webrtc/gloop — if `types` requires such a dep,
stop and record the target in the Phase-2 backlog instead (it should not; `types`
is platform primitives).

- [ ] **Step 4: Verify it builds**

Run:
```bash
/home/vardhv/.local/bin/bazel build $FLAGS //internal/platform/implementation/g3:types 2>&1 | tail -3
```
Expected: `Build completed successfully` (or the target listed as built).

- [ ] **Step 5: Commit**

```bash
git add internal/platform/implementation/g3/
git commit -m "build(test): vendor g3:types fake-platform target from upstream"
```

---

### Task 4: Fix the `@@protobuf+//json` reference

**Files:**
- Modify: `MODULE.bazel` (or the BUILD referencing the json target)

**Interfaces:**
- Consumes: nothing.
- Produces: targets that transitively referenced `protobuf+//json` analyze.

- [ ] **Step 1: Locate the reference**

Run:
```bash
cd /home/vardhv/Desktop/scotty
grep -rn "protobuf.*json\|@@protobuf" --include=BUILD --include=*.bazel . | grep -i json | head
/home/vardhv/.local/bin/bazel build --nobuild $FLAGS //connections/implementation:pcp_manager_test 2>&1 | grep -i "protobuf.*json"
```

- [ ] **Step 2: Resolve to the correct protobuf json target**

Determine the protobuf module's actual json target name for the pinned protobuf
version (inspect the protobuf module):
```bash
/home/vardhv/.local/bin/bazel query 'kind("cc_library", @protobuf//...)' 2>/dev/null | grep -i json | head
```
Repoint the offending dep to the valid label (e.g. `@protobuf//:json_util` or the
name the query returns). Prefer fixing the dep label in the fork's BUILD; only
touch `MODULE.bazel` if the json target requires a module extension not present.

- [ ] **Step 3: Verify the previously-failing target analyzes past json**

Run:
```bash
/home/vardhv/.local/bin/bazel build --nobuild $FLAGS //connections/implementation:pcp_manager_test 2>&1 | grep -i "protobuf.*json" || echo "json ref resolved"
```
Expected: `json ref resolved`.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "build(test): repoint protobuf json dep to a valid label"
```

---

### Task 5: First green test — `base_endpoint_channel_test` (L2CAP read path)

**Files:**
- Test target: `//connections/implementation:base_endpoint_channel_test`

**Interfaces:**
- Consumes: Tasks 2–4 fixes.
- Produces: proof the connections-logic layer builds + runs on the g3 platform.

- [ ] **Step 1: Build the target**

Run:
```bash
cd /home/vardhv/Desktop/scotty
/home/vardhv/.local/bin/bazel build $FLAGS //connections/implementation:base_endpoint_channel_test 2>&1 | tail -5
```
Expected: builds. If it fails on a *new* unvendored package, record it: if it's
webrtc/gloop/fuzz → this target moves to Phase-2 backlog and Task 5 switches to
`//connections/implementation:endpoint_channel_test`; otherwise apply the same
stub/vendor pattern (Tasks 2–4) for the new small dep and note it.

- [ ] **Step 2: Run the test**

Run:
```bash
/home/vardhv/.local/bin/bazel test $FLAGS --test_output=errors //connections/implementation:base_endpoint_channel_test 2>&1 | tail -8
```
Expected: `PASSED`. (This exercises the L2CAP read path our `kEnableBleL2cap`
change rides on.)

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "test: first green connections-logic test (base_endpoint_channel)"
```

---

### Task 6: Green the rest of the connections-logic curated set

**Files:**
- Test targets: `pcp_handler_test`, `pcp_manager_test`, `bwu_test`,
  `mediums:core_internal_mediums_test`, `mediums:bwu_handler_test`,
  `mediums/ble:ble_test`, `mediums/ble:ble_socket_test`
  (restricted to Task 1's `CANDIDATES`).

**Interfaces:**
- Consumes: Task 5 (fixes proven on one target).
- Produces: the connections-logic half of the suite green.

- [ ] **Step 1: Build + test each candidate**

Run (drop any target Task 1 marked `DEFER_PHASE2`):
```bash
cd /home/vardhv/Desktop/scotty
/home/vardhv/.local/bin/bazel test $FLAGS --keep_going --test_output=errors \
  //connections/implementation:pcp_handler_test \
  //connections/implementation:pcp_manager_test \
  //connections/implementation:bwu_test \
  //connections/implementation/mediums:core_internal_mediums_test \
  //connections/implementation/mediums:bwu_handler_test \
  //connections/implementation/mediums/ble:ble_test \
  //connections/implementation/mediums/ble:ble_socket_test 2>&1 | tail -20
```
Expected: all `PASSED`. `pcp_handler_test` specifically covers `kEnableBleL2cap=true`.

- [ ] **Step 2: For any straggler, apply the small-dep pattern**

If a target fails analysis on a *small* unvendored package, resolve it with the
Task 2/3/4 pattern (stub constraint gating / vendor the small target / fix the
label) and re-run. If it needs webrtc/gloop/fuzz, remove it from the set and add
it to the Phase-2 backlog note (Task 9).

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "test: green the connections-logic curated set (incl. pcp_handler flag coverage)"
```

---

### Task 7: Green the Linux bindings + sharing tests

**Files:**
- Test targets: `//internal/platform/implementation/linux:impl_test`,
  `//internal/platform/implementation/linux/tests:linux_connections_test`,
  buildable `//sharing:*` from Task 1.

**Interfaces:**
- Consumes: Tasks 2–4 fixes.
- Produces: the Linux-bindings + sharing half of the suite green.

- [ ] **Step 1: Build + test the Linux platform targets**

Run:
```bash
cd /home/vardhv/Desktop/scotty
/home/vardhv/.local/bin/bazel test $FLAGS --keep_going --test_output=errors \
  //internal/platform/implementation/linux:impl_test \
  //internal/platform/implementation/linux/tests:linux_connections_test 2>&1 | tail -20
```
Expected: `PASSED`. (`impl_test` includes `ble_l2cap_socket_test` /
`ble_l2cap_connection_test` — Scotty's own bindings.)

- [ ] **Step 2: Build + test the buildable sharing targets**

Run the same `bazel test` on each `//sharing:*` target Task 1 marked buildable.
Apply the small-dep pattern for stragglers; defer webrtc-tainted ones.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "test: green the Linux bindings + sharing curated set"
```

---

### Task 8: `scripts/run-tests.sh` runner

**Files:**
- Create: `scripts/run-tests.sh`

**Interfaces:**
- Consumes: the finalized green target list from Tasks 5–7.
- Produces: one command that runs the whole curated suite; consumed by CI (Task 9).

- [ ] **Step 1: Write the script**

Create `scripts/run-tests.sh` (fill `TARGETS` with exactly the green set):

```bash
#!/usr/bin/env bash
# Curated regression suite for the Scotty fork. Single source of truth for
# which tests run — CI calls this same script. See
# docs/superpowers/specs/2026-08-25-test-suite-revival-design.md
set -euo pipefail
cd "$(dirname "$0")/.."

BAZEL="${BAZEL:-bazel}"
FLAGS=(
  --check_visibility=false
  --spawn_strategy=standalone
  --cxxopt=-std=c++20 --host_cxxopt=-std=c++20
  --@com_google_protobuf//bazel/toolchains:prefer_prebuilt_protoc=true
  --copt=-fvisibility=hidden --cxxopt=-fvisibility-inlines-hidden
  --test_output=errors --test_summary=short --keep_going
)

TARGETS=(
  # connections-logic (g3 fake platform)
  //connections/implementation:base_endpoint_channel_test
  //connections/implementation:endpoint_channel_test
  //connections/implementation:pcp_handler_test
  //connections/implementation:pcp_manager_test
  //connections/implementation:bwu_test
  //connections/implementation/mediums:core_internal_mediums_test
  //connections/implementation/mediums:bwu_handler_test
  //connections/implementation/mediums/ble:ble_test
  //connections/implementation/mediums/ble:ble_socket_test
  # Linux bindings + sharing
  //internal/platform/implementation/linux:impl_test
  //internal/platform/implementation/linux/tests:linux_connections_test
  # (append buildable //sharing:* targets confirmed in Task 7)
)

exec "$BAZEL" test "${FLAGS[@]}" "${TARGETS[@]}"
```

Make executable: `chmod +x scripts/run-tests.sh`. Remove any target that Task 1/6/7
deferred to Phase 2.

- [ ] **Step 2: Run it**

Run:
```bash
BAZEL=/home/vardhv/.local/bin/bazel ./scripts/run-tests.sh 2>&1 | tail -15
```
Expected: exit 0, all targets `PASSED`.

- [ ] **Step 3: Commit**

```bash
git add scripts/run-tests.sh
git commit -m "test: add scripts/run-tests.sh (single source of truth for the suite)"
```

---

### Task 9: GitHub Actions workflow + docs

**Files:**
- Create: `.github/workflows/test.yml`
- Create: `docs/testing.md`

**Interfaces:**
- Consumes: `scripts/run-tests.sh`.
- Produces: CI enforcement + contributor docs.

- [ ] **Step 1: Inspect the existing release workflow for the bazel-setup pattern**

Run:
```bash
ls .github/workflows/
sed -n '1,80p' .github/workflows/*release* 2>/dev/null
```
Reuse its bazel install/version and cache steps.

- [ ] **Step 2: Write the workflow**

Create `.github/workflows/test.yml`:

```yaml
name: tests
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
jobs:
  bazel-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Cache bazel
        uses: actions/cache@v4
        with:
          path: ~/.cache/bazel
          key: bazel-${{ runner.os }}-${{ hashFiles('MODULE.bazel', '.bazelversion') }}
          restore-keys: bazel-${{ runner.os }}-
      - name: Install bazelisk
        run: |
          curl -fsSL -o /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64
          chmod +x /usr/local/bin/bazel
      - name: Run curated test suite
        run: BAZEL=bazel ./scripts/run-tests.sh
```
Match the bazel version to `.bazelversion` if present (adjust the install step to
pin it). If the release workflow installs system deps (e.g. Qt, protobuf tools)
needed for these targets, copy those apt steps in.

- [ ] **Step 3: Write the docs note**

Create `docs/testing.md` covering: how to run locally (`./scripts/run-tests.sh`),
how to add a target (append to `TARGETS`, must build without webrtc/gloop/fuzz),
and the **Phase-2 backlog**: vendor webrtc/gloop, build fuzz targets, and the
specific targets deferred in Tasks 1/5/6/7.

- [ ] **Step 4: Validate the workflow locally**

Run a YAML lint:
```bash
python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/test.yml')); print('yaml ok')"
```
Expected: `yaml ok`.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/test.yml docs/testing.md
git commit -m "ci: run the curated test suite on push/PR; add docs/testing.md"
```

---

### Task 10: Prove the guard bites, then land the branch

**Files:**
- Temporary edit (reverted): any file with a checked behavior.

**Interfaces:**
- Consumes: the green suite.
- Produces: evidence the suite actually catches a regression; a mergeable branch.

- [ ] **Step 1: Introduce a deliberate regression**

Pick a behavior a curated test asserts (e.g. invert a boolean in a medium/flag
check in `connections/implementation/`). Do NOT commit it.

- [ ] **Step 2: Confirm the suite goes red**

Run:
```bash
BAZEL=/home/vardhv/.local/bin/bazel ./scripts/run-tests.sh 2>&1 | tail -15
```
Expected: at least one target `FAILED`, script exits nonzero. Record which test caught it.

- [ ] **Step 3: Revert the regression and confirm green**

Run:
```bash
git checkout -- <the file>
BAZEL=/home/vardhv/.local/bin/bazel ./scripts/run-tests.sh 2>&1 | tail -5
```
Expected: exit 0, all `PASSED`.

- [ ] **Step 4: Record the proof in docs/testing.md**

Append a short "guard verified" note (date, which test caught the injected break).

- [ ] **Step 5: Commit and push the branch for CI**

```bash
git add docs/testing.md
git commit -m "test: verify the suite catches an injected regression"
git push -u origin test-suite-revival
```
Then confirm the GitHub Actions `tests` job goes green on the branch before merging to `main`.

---

## Self-Review

- **Spec coverage:** curated subset (Tasks 5–8) ✓; both layers — connections-logic (Tasks 5–6) + Linux/sharing (Task 7) ✓; approach A dep fixes — buildenv (Task 2), g3:types (Task 3), protobuf-json (Task 4) ✓; webrtc/fuzz excluded (Global Constraints + defer steps) ✓; runner script (Task 8) ✓; CI (Task 9) ✓; guard-bites proof (Task 10) ✓; Phase-2 backlog doc (Task 9) ✓; audit-driven final list (Task 1) ✓.
- **Placeholders:** none — the only deferred specifics (exact g3:types files, exact protobuf-json label, final target list) are resolved by explicit probe commands within Tasks 1/3/4, which is the correct method for vendored-from-upstream BUILD content.
- **Type/name consistency:** `CANDIDATES` (Task 1) feeds Tasks 5–8; `scripts/run-tests.sh` `TARGETS` matches the Task 5–7 green set; the bazel `FLAGS` are identical everywhere (Global Constraints).
