# `sakura-slemu` — complete reference

> A Sakura Studios, IKE project · maintained by Shiho Sakura
> ([@ShihoSakura](https://github.com/ShihoSakura)) · MIT-licensed.

`sakura-slemu` is the runtime half of Sakura Studios' LSL toolchain.
Its companion is [`sakura-lslc`](https://github.com/ShihoSakura/sakura-lslc),
the offline compiler; their interface is the SLBC bytecode format.

This document covers:

1. [Pipeline overview](#1--pipeline-overview)
2. [Build](#2--build)
3. [CLI reference](#3--cli-reference)
4. [Bytecode format (SLBC)](#4--bytecode-format-slbc)
5. [Runtime semantics](#5--runtime-semantics)
6. [Built-in coverage](#6--built-in-coverage)
7. [Project volume](#7--project-volume)
8. [HTTP backend](#8--http-backend)
9. [Tracing & debugging](#9--tracing--debugging)
10. [Limitations](#10--limitations)
11. [Author / Attribution](#11--author--attribution)

---

## 1 — Pipeline overview

```
.lsl  ──[ lslc -c ]──>  .lslbc  ──[ slemu ]──>  simulated execution
                                                + persistent project volume
                                                + listen/link-message routing
                                                + L$ economy
                                                + HTTP (fixture or real)
```

The compiler stops after semantic analysis + constant folding succeeds
and emits a compact binary serialisation of the typed AST. The runtime
loads that AST and walks it with an event-loop wrapper that mimics a
Second Life region.

The two repos are independent: `sakura-lslc` produces `.lslbc` files
that any compatible runtime can consume; `sakura-slemu` is the
reference one.

## 2 — Build

```sh
make                                      # gcc / clang / tcc, Linux/macOS/*BSD
mingw32-make                              # MinGW
cmake -B build && cmake --build build     # cross-platform, includes MSVC
```

The only runtime dependency outside libc is `libm`. With
`--http-real`, the system `curl` binary is invoked at request time
(no linkage; just a fork+exec).

## 3 — CLI reference

```
slemu [options] script.lslbc [script2.lslbc ...]
```

Each positional argument becomes a linked prim in the simulated
object. The first script is the root (link 1), subsequent scripts are
children (link 2, 3, …).

Options:

| Flag                          | Default            | Effect |
|-------------------------------|--------------------|--------|
| `--volume DIR`                | `./slemu_volume`   | Project volume (created if absent). |
| `--owner UUID[:Name]`         | random             | Owner avatar key + display name. |
| `--owner-balance N`           | 0                  | Owner's starting L$ balance. |
| `--avatar UUID:BAL[:Name]`    | —                  | Pre-register another avatar; repeatable. |
| `--name NAME`                 | `Object`           | Result of `llGetObjectName()`. |
| `--steps N`                   | 100 000            | Max dispatched events. |
| `--timeout SECS`              | 60                 | Wall-clock cap. |
| `--trace`                     | off                | Log every event dispatch. |
| `--no-http`                   | on                 | HTTP via fixture file. |
| `--http-real`                 | off                | HTTP via system `curl`. |
| `--http-fixture FILE`         | —                  | Fixture path (URL substring match). |
| `--version`                   |                    | Print version. |
| `-h`, `--help`                |                    | Show help. |

Exit codes:

| Code | Meaning |
|------|---------|
| 0    | Normal exit (script ran to completion or hit step/timeout cap). |
| 2    | Bad CLI / I/O / cannot load a `.lslbc`. |

## 4 — Bytecode format (SLBC)

The format is documented exhaustively at the top of
`../sakura-lslc/src/emit.c`. Summary:

```
header     SLBC\0   u32 version   u32 flags(bit0 = LSO)
strings    u32 N    [u32 length, bytes[length]] x N
globals    u32 N    [u8 type, u32 name_idx, u8 has_init, expr?] x N
funcs      u32 N    [u32 name_idx, u8 ret, u8 has_ret, u8 nparams, params, stmt] x N
states     u32 N    [u32 name_idx, u8 is_default, u32 nevents, events] x N
```

Every expression / statement is a tagged-tree node whose tag is the
matching enum value from the compiler's `ExprKind` / `StmtKind`. See
`src/loader.c` in this repo for the reader.

If a future version of the compiler bumps the format the version field
goes up and the loader rejects mismatches.

## 5 — Runtime semantics

* **Globals** are initialised in source order; only constant
  initialisers are accepted (the compiler enforces this).
* **State machine** starts in the `default` state. `state X;`
  finishes the current event, flushes the queue, runs `state_exit` on
  the prior state, then runs `state_entry` on the new one.
* **Event queue** is FIFO per script. Inter-script events
  (`link_message`, `listen`) enqueue across all targets.
* **Timers** are tracked at virtual-time intervals; they fire by
  enqueuing a `timer` event when due. Multiple due timers in a single
  tick fire one each per cycle.
* **L$ economy**: every avatar UUID known to the region has a balance.
  `llGiveMoney` requires `PERMISSION_DEBIT` (auto-granted in slemu)
  and atomically debits owner / credits recipient. Insufficient funds
  return 0; otherwise 1.
  `llTransferLindenDollars` does the same and fires a
  `transaction_result` event afterwards.
* **Permissions**: `llRequestPermissions` is auto-approved — the
  runtime immediately enqueues `run_time_permissions(perms)`.
* **Listen routing**: when any script emits via `llSay` /
  `llWhisper` / `llShout` / `llRegionSay`, every active `llListen`
  filter across every loaded script is checked; matching filters
  receive a `listen(channel, name, id, msg)` event. `llRegionSayTo`
  delivers only to the prim whose UUID matches.
* **link_message**: routed by `llMessageLinked(target, num, str, id)`
  exactly as in SL: `LINK_SET` / `LINK_ALL_OTHERS` /
  `LINK_ALL_CHILDREN` / `LINK_THIS` / explicit numeric link.
* **Sleep**: `llSleep(seconds)` is a real wall-clock sleep, so
  step-/timeout-caps apply.
* **llDie / llResetScript**: `llDie` drops the script's events &
  timer and the script becomes inert. `llResetScript` reruns
  initialisers and queues `state_entry` on the default state.

## 6 — Built-in coverage

This table is partial — see `src/builtins.c` for the full registry.

| Category | Implemented |
|----------|-------------|
| I/O | llSay, llOwnerSay, llWhisper, llShout, llRegionSay, llRegionSayTo, llInstantMessage, llSetText, llDialog, llTextBox, llLoadURL |
| Strings | llStringLength, llSubStringIndex, llGetSubString, llDeleteSubString, llInsertString, llToLower, llToUpper, llStringTrim, llChar, llOrd |
| Lists | llGetListLength, llList2String/Integer/Float/Key/Vector/Rot, llList2List, llListFindList, llListInsertList, llListReplaceList, llDeleteSubList, llListSort, llDumpList2String, llList2CSV, llCSV2List, llParseString2List, llParseStringKeepNulls, llListRandomize |
| Math | llAbs, llFabs, llFloor, llCeil, llRound, llSqrt, llSin/Cos/Tan/Asin/Acos/Atan2, llPow, llLog, llLog10, llFrand, llVecMag, llVecNorm, llVecDist |
| Time | llGetTime, llResetTime, llGetUnixTime, llGetTimestamp, llGetDate, llGetWallclock, llSleep, llSetTimerEvent, llMinEventDelay |
| Object | llGetKey, llGetOwner, llGetCreator, llGetObjectName, llSetObjectName, llGetObjectDesc, llSetObjectDesc, llDie, llResetScript, llGetScriptName, llGetScriptID, llGetPos, llSetPos, llGetRot, llSetRot, llGetScale, llSetScale, memory probes |
| Linkset | llListen, llListenRemove, llListenControl, llMessageLinked, llGetLinkNumber/Name/Key, llGetNumberOfPrims |
| Detection | llDetectedKey/Name/Owner/Type/Pos/LinkNumber |
| Permissions / $ | llRequestPermissions (auto-grant), llGetPermissions, llGetPermissionsKey, llGiveMoney, llTransferLindenDollars, llGetMyAccountBalance, llSetPayPrice |
| Linkset data | llLinksetDataWrite/Read/Delete/Reset/CountKeys/ListKeys/Available |
| Encoding/hash | llMD5String, llSHA1String, llSHA256String (stub), llStringToBase64, llBase64ToString, llIntegerToBase64, llBase64ToInteger, llEscapeURL, llUnescapeURL, llHMAC, llHash |
| JSON | llJsonGetValue, llJsonValueType, llJsonSetValue, llJson2List, llList2Json |
| HTTP | llHTTPRequest, llHTTPResponse, llSetContentType |
| Region/avatar | llGetRegionName, llKey2Name, llGetUsername, llGetDisplayName, llGetAttached |

Functions outside this table are stubbed: a call logs
`[slemu] (stub) name(...)` under `--trace` and returns the zero value
of the appropriate type. Real production scripts can therefore exercise
their happy paths without aborting; add more implementations to
`builtins.c` (one entry per `BuiltinEntry` plus a `TABLE` row) as you
need them.

Built-in constants implemented inline in `builtins_const()`:
`TRUE`, `FALSE`, `NULL_KEY`, `EOF`, `PI`, `TWO_PI`, `PI_BY_TWO`,
`DEG_TO_RAD`, `RAD_TO_DEG`, `SQRT2`, `PUBLIC_CHANNEL`, `DEBUG_CHANNEL`,
the `HTTP_*` option keys, the `PERMISSION_*` mask, the `CHANGED_*`
flags, the `STRING_TRIM*` set, the `JSON_*` sentinels, the `LINK_*`
constants, `ALL_SIDES`, the `INVENTORY_*` enum, `PAY_HIDE` /
`PAY_DEFAULT`, `STATUS_OK`, the `LINKSETDATA_*` action codes.

## 7 — Project volume

```
<volume>/
├── economy.txt          tab-separated: uuid \t balance \t name
└── lsd/
    └── <key>.txt        one file per linkset-data key
```

Inspect by hand, edit, version-control. The volume is loaded at start
and re-saved on graceful exit (after a step cap, after timeout, or after
all scripts settle).

## 8 — HTTP backend

Two modes:

1. **Fixture (default)** — `--http-fixture file` matches an outgoing
   URL against each entry's `URL_MATCH` (substring). Format:

   ```
   URL_MATCH https://example.com/path
   STATUS 200
   BODY {"ok":true}

   URL_MATCH https://example.com/fail
   STATUS 500
   BODY oops
   ```

2. **Real** — `--http-real` shells out to `curl` (or
   `curl.exe` on Windows under MinGW). Method (`HTTP_METHOD`) and
   mimetype (`HTTP_MIMETYPE`) are honoured; custom headers are
   currently dropped.

Either way, the result flows back as an `http_response(key id, integer
status, list metadata, string body)` event.

## 9 — Tracing & debugging

```
slemu --trace …
```

Logs to stderr a line for each dispatched event, with all arguments
already stringified:

```
[slemu] root.default.state_entry()
[slemu] child.default.state_entry()
[slemu] root.default.listen(7, child, 33....., ping)
```

Pair with `--steps N` to step through deterministically.

## 10 — Limitations

* Some rarer built-ins are stubs; see "Built-in coverage" above.
* SHA-256 is implemented as a deterministic placeholder (uses SHA-1
  internally). Real SHA-256 is a TODO if you need exact compatibility
  with a server expecting LL's hash.
* Camera, vehicle, and particle systems are accepted but visually
  unobservable (this is a *headless* emulator).
* Physics is not simulated; `llSetPos`/`llSetRot`/`llGetPos` return
  static defaults.
* Sensor events are not auto-fired; you can hand-queue them through
  custom test harnesses if needed.
* The lexer / loader read SLBC v1 only; if the compiler bumps the
  version this runtime will need a matching update.

## 11 — Author / Attribution

`sakura-slemu` is © 2026 **Sakura Studios, IKE**, authored and
maintained by **Shiho Sakura**
([@ShihoSakura](https://github.com/ShihoSakura)), and distributed
under the **MIT License** — see [`LICENSE`](./LICENSE).

The LSL language and its built-in surface are © Linden Research, Inc.
The tables in this codebase reference them as factual data, not
creative content.

The companion compiler lives at
[github.com/ShihoSakura/sakura-lslc](https://github.com/ShihoSakura/sakura-lslc).
