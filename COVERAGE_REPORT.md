# sakura-slemu — runtime coverage report

This document enumerates the runtime-coverage scenarios in
[`tests/coverage/`](tests/coverage/). Each scenario consists of one or
two `.lsl` source files, an optional `.cmds` driver, an optional
`.fixture` HTTP fixture, and a `expect/<name>.txt` whose lines must
appear (as substrings, in order) in slemu's `--json-events` stdout.

Run the full suite with:

```sh
make                                           # ensure ./slemu is built
tests/coverage/run_coverage.sh ./slemu
```

The runner compiles every `.lsl` with `../sakura-lslc/lslc -c`, then
invokes slemu for every scenario:

```
./slemu --json-events --steps 500 --timeout 5 \
        --volume /tmp/slemu_cov_<name> \
        --commands tests/coverage/<name>.cmds \
        --owner-balance 100 \
        [--http-fixture tests/coverage/<name>.fixture] \
        tests/coverage/<name>.lslbc [tests/coverage/<name>_b.lslbc]
```

## Result

**41 / 41 scenarios passing.**

## Coverage matrix

| #  | Scenario                       | Category         | Built-ins / events exercised |
|----|--------------------------------|------------------|------------------------------|
| 01 | `01_chat_channels`             | I/O              | `llSay`, `llWhisper`, `llShout`, `llRegionSay` on +/- channels |
| 02 | `02_owner_say`                 | I/O              | `llOwnerSay` (kind=owner, ch=0) |
| 03 | `03_region_say_to`             | I/O              | `llRegionSayTo` (kind=region-to, to=uuid) |
| 04 | `04_instant_message`           | I/O              | `llInstantMessage` (kind=im, ch=-1) |
| 05 | `05_strings`                   | Strings          | `llStringLength`, `llSubStringIndex`, `llGetSubString` (+/-/wrap), `llDeleteSubString`, `llInsertString`, `llStringTrim` (all modes), `llToLower`, `llToUpper`, `llChar`, `llOrd` |
| 06 | `06_lists`                     | Lists            | `llGetListLength`, `llList2String/Integer/Float/Key/Vector/Rot`, `llList2List` (+/- slice), `llListFindList`, `llListInsertList`, `llListReplaceList`, `llDeleteSubList`, `llListSort` (asc/desc), `llDumpList2String`, `llList2CSV`, `llCSV2List`, `llParseString2List` (sep+spacer), `llParseStringKeepNulls`, `llListRandomize` |
| 07 | `07_math`                      | Math             | `llSin`, `llCos`, `llTan`, `llAsin`, `llAcos`, `llAtan2`, `llSqrt`, `llPow`, `llLog`, `llLog10`, `llFloor`, `llCeil`, `llRound`, `llAbs`, `llFabs`, `llFrand`, `llVecMag`, `llVecNorm`, `llVecDist` |
| 08 | `08_time_basic`                | Time             | `llGetUnixTime`, `llGetTimestamp`, `llGetDate` |
| 09 | `09_timer`                     | Time             | `llSetTimerEvent` schedules timer; `timer` event fires N times; `llSetTimerEvent(0.0)` cancels |
| 10 | `10_sleep`                     | Time             | `llSleep` actually pauses; `llGetTime` advances across dispatches |
| 11 | `11_object_identity`           | Object           | `llGetKey`, `llGetOwner`, `llGetCreator`, `llGetObjectName`, `llSetObjectName`, `llGetObjectDesc`, `llSetObjectDesc`, `llGetScriptName`, `llGetScriptID`, `llGetPos`, `llSetPos` (no-crash) |
| 12 | `12_listen_basic`              | Listen           | `llListen(ch,"",NULL_KEY,"")` + LISTEN cmd -> listen event |
| 13 | `13_listen_filter`             | Listen           | Listen with name+msg filters; non-matching dropped |
| 14 | `14_listen_remove`             | Listen           | `llListenRemove` drops subsequent matches |
| 15 | `15_listen_control`            | Listen           | `llListenControl` off/on round-trip |
| 16 | `16_detection`                 | Detection        | `llDetectedKey`, `llDetectedName`, `llDetectedOwner`, `llDetectedType`, `llDetectedPos`, `llDetectedLinkNumber` (TOUCH_AS) |
| 17 | `17_perms_debit`               | Permissions      | `llRequestPermissions(PERMISSION_DEBIT)` auto-grants `run_time_permissions` |
| 18 | `18_give_money_ok`             | $ economy        | `llGiveMoney` with sufficient balance: debit, credit, returns 1 |
| 19 | `19_give_money_fail`           | $ economy        | `llGiveMoney` with insufficient balance returns 0 |
| 20 | `20_transfer_ld`               | $ economy        | `llTransferLindenDollars` fires `transaction_result` |
| 21 | `21_account_balance`           | $ economy        | `llGetMyAccountBalance` returns owner's balance |
| 22 | `22_lsd_same_run`              | Linkset Data     | Write then read within one run |
| 23 | `23_lsd_persist`               | Linkset Data     | Two-pass: pass 1 writes, pass 2 reads from the same volume |
| 24 | `24_lsd_delete_count_list`     | Linkset Data     | `llLinksetDataReset`, `Write`, `CountKeys`, `ListKeys`, `Delete` |
| 25 | `25_encoding`                  | Encoding / hash  | `llMD5String` (known hex), `llSHA1String` (known hex), `llSHA256String` (length stub), `llStringToBase64` ↔ `llBase64ToString`, `llIntegerToBase64` ↔ `llBase64ToInteger`, `llEscapeURL` ↔ `llUnescapeURL`, `llHMAC` (determinism + length), `llHash` (determinism) |
| 26 | `26_json`                      | JSON             | `llJsonGetValue`, `llJsonValueType`, `llJson2List`, `llList2Json` (object + array round-trips) |
| 27 | `27_http_out_ok`               | HTTP outbound    | `llHTTPRequest` with fixture match → `http_response(status=200, body=…)` |
| 28 | `28_http_out_miss`             | HTTP outbound    | `llHTTPRequest` with no fixture match → `http_response(status=0, …)` |
| 29 | `29_http_in_url`               | HTTP inbound     | `llRequestURL` synchronously fires `http_request(method="URL_REQUEST_GRANTED", body=url)` with the slemu-local URL prefix |
| 30 | `30_http_in_post`              | HTTP inbound     | URL_REQUEST_GRANTED dispatch path + HTTP_IN to unregistered URL → "no script listening" info event (see note below) |
| 31 | `31_set_text_hud`              | HUD              | `llSetText` emits `hud` event with text + RGB + alpha |
| 32 | `32_dialog_reply`              | Dialog           | `llDialog` opens, `ASSERT_DIALOG_OPEN`, `DIALOG_REPLY` → `listen` event; second `ASSERT_DIALOG_OPEN` fails because the dialog closed |
| 33 | `33_textbox_reply`             | Dialog           | `llTextBox` opens, `ASSERT_DIALOG_OPEN`, `TEXTBOX_REPLY` → `listen` event |
| 34 | `34_states`                    | State machine    | default → waiting → default with both `state_entry` and `state_exit` in order |
| 35 | `35_on_rez`                    | Events           | `ON_REZ <p>` fires `on_rez(p)` |
| 36 | `36_changed`                   | Events           | `CHANGED <flags>` fires `changed(flags)` |
| 37 | `37_attach_detach`             | Events           | `ATTACH <uuid> <pt>` then `DETACH` fires `attach(id)` twice |
| 38 | `38_link_msg`                  | Multi-script     | Two linked scripts; root sends `llMessageLinked(LINK_ALL_OTHERS, …)`, child receives via `link_message` |
| 39 | `39_link_meta`                 | Multi-script     | `llGetLinkNumber`, `llGetLinkName`, `llGetLinkKey`, `llGetNumberOfPrims` across two prims |
| 40 | `40_link_chat`                 | Multi-script     | Root says on channel; child llListen'ing receives `listen` |
| 41 | `41_region_world`              | Region / avatar  | `llGetRegionName`, `llKey2Name`, `llGetUsername` (with owner-name lookup); unknown key returns empty |

### Scenario 30 — note on inbound HTTP

`llRequestURL` returns a non-deterministic UUID-based URL per slemu
process, and `--commands` is loaded before that URL exists, so static
cmds cannot positively deliver to a `llRequestURL`-registered URL.
Scenario 30 therefore exercises:

1. the dispatch of `http_request(method="URL_REQUEST_GRANTED", body=url)`,
   which uses the same `script_push_event(s,"http_request",…)` path the
   positive HTTP_IN case would take, and
2. the `HTTP_IN` no-listener branch (info event
   `"HTTP_IN: no script listening on …"`).

Together these prove both edges of the HTTP_IN command's behaviour.

## Files

```
tests/coverage/
├── 01_chat_channels.lsl / .cmds / expect/01_chat_channels.txt
├── …
├── 41_region_world.lsl / .cmds / expect/41_region_world.txt
├── run_coverage.sh          # the runner described above
└── expect/                  # one .txt per scenario
```

Multi-script scenarios use `<base>_a.lsl` (root) + `<base>_b.lsl`
(child) plus a single `<base>.cmds` driver and `<base>.txt` expect
file:

```
38_link_msg_a.lsl
38_link_msg_b.lsl
38_link_msg.cmds
expect/38_link_msg.txt
```

## Adding a scenario

1. Drop `NN_name.lsl` (and optionally `NN_name.cmds`,
   `NN_name.fixture`) into `tests/coverage/`.
2. Add `tests/coverage/expect/NN_name.txt` — one substring per line, in
   the order they should appear in slemu's stdout.
3. (Optional) For a multi-script test use `NN_name_a.lsl` + `NN_name_b.lsl`.
4. Re-run `tests/coverage/run_coverage.sh ./slemu`.
