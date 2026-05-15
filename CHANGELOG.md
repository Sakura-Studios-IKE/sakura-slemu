# sakura-slemu — Changelog

All notable changes are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).
The `[Unreleased]` section is what's on `main`; the release pipeline
promotes it to a numbered version on tag.

## [Unreleased]

## [1.0.0] — 2026-05-15

### Added
- Initial release of the Sakura SLBC emulator: loads `.lslbc` produced
  by `sakura-lslc` and runs it against a simulated Second Life region.
- Bytecode loader (`src/loader.c`) and stack-based VM (`src/vm.c`)
  with full LSL value model (`src/value.c`).
- Built-in implementations across the LSL standard library
  (`src/builtins.c`): chat (`llSay`, `llWhisper`, `llShout`,
  `llRegionSay`, `llRegionSayTo`, `llOwnerSay`, `llInstantMessage`),
  listen filters, strings, lists, math, time/timer/sleep,
  object identity, detection, money + permissions, linkset data,
  encoding, JSON, HTTP in/out, set-text, dialog/textbox, states,
  on_rez, changed, attach/detach, link messages.
- Region simulation (`src/region.c`) with avatars, prims, link sets,
  inventory, time-of-day, and prim metadata.
- Persistent on-disk world volume (`src/volume.c`) so region state and
  linkset-data survive across runs.
- HTTP client (`src/http.c`) with both fixture-driven offline mode and
  a `--http-real` mode that actually hits the network.
- End-to-end test harness: `tests/run_e2e.sh` with five scripted
  scenarios (hello, money, link parent/child, HTTP+LSD, full
  interaction).
- Interactive command channel for driving the running region from
  scripts/fixtures (`src/commands.c`) — chat as avatars, touch, pay,
  attach/detach, rez, link, etc.
- Configurable world (`src/config.c`): owner UUID/name/balance,
  extra avatars, region name, starting time, all overridable from
  CLI or `.cfg` file.
- Dialog / textbox / HUD-text plumbing (`src/dialog.c`) and a
  player-action API used by `lsltest` to simulate user input.
- JSON event stream (`src/events.c`): every visible event (chat,
  set-text, dialog, money, link, rez, attach…) emitted as a single
  JSON line on stdout for tooling to consume (used by the IntelliJ
  plugin's Emulator tool window).
- `--debug` protocol that lets `lsldb` attach over a socket, step
  through bytecode, set breakpoints, and inspect locals/globals
  (`src/dbg.c`).
- Source-line numbers in stack traces (SLBC v2) so faults point to
  `.lsl` lines, not raw bytecode offsets.
- Coverage corpus: 41 scripted scenarios under `tests/coverage/`
  covering chat, listen, money, LSD, HTTP, JSON, encoding, dialog,
  states, on_rez, changed, attach, link messages and region world,
  plus `tests/coverage/run_coverage.sh` and `COVERAGE_REPORT.md`.

### Changed
- README cross-references the other four toolchain repos and reports
  current test counts.

### Fixed
- Several regressions surfaced by the `lsltest` Wave 1C scenarios in
  `src/commands.c` and `src/region.c`.
- `--http-real`: temp-file path was being written as the literal
  string `$$` instead of the shell-expanded PID, breaking real HTTP
  outbound calls (`src/http.c`).

### Removed
- Regenerated `.lslbc` artefacts under `tests/coverage/` are no longer
  tracked in git; they're produced by `run_coverage.sh` from the
  matching `.lsl` source. `.gitignore` updated accordingly.
