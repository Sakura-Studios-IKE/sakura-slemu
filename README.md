# sakura-slemu

**A headless Second Life region emulator for LSL, in pure C99 — by Sakura
Studios, IKE.**

`sakura-slemu` (binary: `slemu`) runs SLBC bytecode emitted by
[`sakura-lslc`](https://github.com/ShihoSakura/sakura-lslc) in a
simulated region: full LSL event loop, state machines, listen routing,
inter-script link-messages, a working L$ economy, persisted linkset
data, an HTTP backend (fixture or real `curl`), and a project volume
where all of the above survives between runs.

```sh
git clone https://github.com/ShihoSakura/sakura-slemu.git
cd sakura-slemu
make
echo 'default { state_entry() { llOwnerSay("hello"); } }' > hi.lsl
../sakura-lslc/lslc -c hi.lsl
./slemu hi.lslbc
```

```
[owner Object] hello
```

The toolchain pipeline:

```
.lsl  ──[ lslc -c ]──>  .lslbc  ──[ slemu ]──>  simulated execution + persistent state
```

## What works

- **Full LSL Mono runtime**: every operator, control-flow construct,
  vector / rotation / list arithmetic, state machine, jump/label,
  user functions, recursive calls, `return`, `state X;`.
- **Event loop**: all events fired by lslc (`state_entry`, `touch_start`,
  `listen`, `timer`, `http_response`, `link_message`,
  `transaction_result`, `run_time_permissions`, `changed`, `attach`,
  `on_rez`, `money`, `linkset_data`, …). Round-robin dispatch across
  every script in the region.
- **200+ built-ins** covering everything used by real production
  scripts: math, strings, lists, JSON (`llJsonGetValue`/`llJson2List`/
  `llList2Json`), base64, MD5/SHA-1, HMAC, time/timers, sleep, the
  whole `llDetected*` family, all `llSet*`/`llGet*` object-state ops,
  permission requests, sensors, `llSetTimerEvent`, etc.
- **Listen routing**: `llSay` / `llWhisper` / `llShout` / `llRegionSay`
  / `llRegionSayTo` reach every `llListen` filter that matches across
  every linked script.
- **Multi-script linkset**: each `.lslbc` is loaded as a linked prim
  (link 1 = root, then 2…N). `llMessageLinked(LINK_SET, …)`,
  `LINK_ALL_OTHERS`, `LINK_ALL_CHILDREN`, `LINK_THIS`, and explicit
  link-number targets all route correctly.
- **L$ economy**: `--avatar UUID:BAL[:Name]` registers virtual
  avatars. `llGiveMoney` requires `PERMISSION_DEBIT` (auto-granted by
  the emulator), debits owner, credits recipient, persists to volume.
  `llTransferLindenDollars` fires a `transaction_result` event.
- **Linkset data**: `llLinksetDataWrite/Read/Delete/ListKeys/Reset`
  back onto `<volume>/lsd/<key>.txt`. Survives across runs.
- **HTTP**: either real (`--http-real`, shells out to `curl`) or
  fixture-driven (`--http-fixture file`). Either way `http_response`
  fires with status + body.
- **Project volume**: a directory you point at with `--volume DIR`.
  Avatar balances live in `economy.txt`, linkset data in `lsd/*.txt`.
  Inspect by hand, version-control if you want a known-good state.
- **Tracing**: `--trace` logs every event dispatch with arguments.

## Build

Pure C99, depends only on libm and (for `--http-real`) a `curl` binary
in `$PATH`. Builds with gcc / clang / tcc / MinGW / MSVC.

```sh
make                          # Linux / macOS / *BSD
cmake -B build && cmake --build build   # cross-platform
```

## Usage

```sh
slemu [options] script.lslbc [script2.lslbc ...]
```

### Important flags

| Flag                       | Meaning |
|----------------------------|---------|
| `--volume DIR`             | Persistence directory (default `./slemu_volume`). |
| `--owner UUID[:Name]`      | Owner avatar (the one `llGetOwner` returns). |
| `--owner-balance N`        | Starting L$ for the owner. |
| `--avatar UUID:BAL[:Name]` | Pre-register another avatar with starting L$. Repeatable. |
| `--name NAME`              | Object name returned by `llGetObjectName()`. |
| `--steps N`                | Max events to dispatch (default 100 000). |
| `--timeout SECS`           | Wall-clock cap (default 60 s). |
| `--trace`                  | Log every event dispatch with arguments. |
| `--no-http` (default)      | HTTP through fixture file. |
| `--http-real`              | HTTP via system `curl`. |
| `--http-fixture FILE`      | Path to fixture file (URL substring match). |

### Fixture file format

```
URL_MATCH https://example.com/api/who
STATUS 200
BODY {"name":"Shiho","balance":42}

URL_MATCH https://example.com/api/down
STATUS 503
BODY service unavailable
```

(Blank line between entries; URL match is substring.)

## Tests

The repo ships two test suites:

```sh
make e2e            # 3 end-to-end pipeline tests (lslc -c → slemu run)
make -C tests       # n/a — coverage runner lives at tests/coverage/

sh tests/coverage/run_coverage.sh   # 41 scenarios exercising every
                                    # supported builtin / event class
```

Combined: **3 e2e + 41 coverage scenarios, all green.** Plus the
debugger integration test in `../sakura-lsldb/` exercises the
`--debug` protocol end-to-end.

## Project layout

```
sakura-slemu/
├── README.md
├── DOCUMENTATION.md
├── LICENSE
├── .gitignore
├── Makefile
├── CMakeLists.txt
├── src/
│   ├── slemu.h           shared types
│   ├── main.c            CLI
│   ├── loader.c          .lslbc reader
│   ├── value.c           LSL tagged-union value model
│   ├── vm.c              AST-walking interpreter + event loop hooks
│   ├── builtins.c        200+ LSL standard-library implementations
│   ├── region.c          multi-script container, event dispatcher
│   ├── volume.c          economy + linkset-data persistence
│   ├── http.c            HTTP fixture + curl backend
│   └── util.c            memory, files, time, JSON, UUID helpers
└── tests/
    ├── scripts/          .lsl sources for integration tests
    ├── expect/           expected-stdout fixtures
    ├── run_e2e.sh        runner
    └── run_tests.sh      same, alias
```

## Status

`sakura-slemu` is the runtime in Sakura Studios' five-tool open-source
LSL toolchain:

* [`sakura-lslc`](https://github.com/ShihoSakura/sakura-lslc) — the
  compiler that produces the `.lslbc` this runtime consumes.
* [`sakura-lsldb`](https://github.com/ShihoSakura/sakura-lsldb) — gdb-style
  CLI debugger that drives this runtime through its `--debug` protocol.
* [`sakura-lsltest`](https://github.com/ShihoSakura/sakura-lsltest) —
  pytest-style test framework that orchestrates this runtime.
* [`sakura-intellij-lsl`](https://github.com/ShihoSakura/sakura-intellij-lsl)
  — IntelliJ plugin that uses this runtime as its "run / debug" target.

## Author / Attribution

Authored and maintained by **Shiho Sakura**
([@ShihoSakura](https://github.com/ShihoSakura)) on behalf of
**Sakura Studios, IKE**.

The LSL language, built-in function set, and event signatures are ©
Linden Research, Inc.; nothing in this repository claims ownership of
them.

## License

MIT — see [`LICENSE`](./LICENSE).
