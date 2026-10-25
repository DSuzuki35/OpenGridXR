# Telemetry helper

`OpenGridXR-Telemetry.exe --game auto --rate 20` reads the local iRacing SDK and
Le Mans Ultimate's native shared memory. It opens existing mappings with
`FILE_MAP_READ`; it does not create game mappings, write synchronization data,
consume game events, modify files, send controls, or install a plugin.

Use `--game iracing` or `--game lmu` for one source. `auto` reports both sources
independently. The default rate is 20 Hz **per live source**, with an enforced
1–30 Hz range. Missing mappings retry once a second. Stale sources are polled at
4 Hz; status heartbeats are limited to 1 Hz, except immediate status transitions.
The first snapshot remains stale until a source tick advances. After 1 second
without a source advance, the last reading is explicitly stale. After 5 seconds
the mapping is closed and probed again, allowing a stopped producer's mapping to
disappear. A disconnected/invalid/unsupported source publishes null values.

The helper is intended to run only while the application needs telemetry.
Shutdown accepts `quit\n` or `stop\n` on stdin, stdin pipe closure, Ctrl-C, and
stdout pipe closure. There are no background services or networking. The stdout
write uses pipe backpressure with no application queue; a parent that stops
reading must close the pipe or terminate the child. `--once` reports current
source status and exits. `--duration-ms 1..60000` provides bounded diagnostic runs.

## Source requirements

- **iRacing:** native SDK mapping `Local\IRSDKMemMapFileName`, SDK header version 2.
  No web API, credentials, recording toggle, or additional plugin is required.
  Metadata controls typed variable offsets; descriptors and buffer ranges are
  validated. The newest buffer is copied and its tick checked again, with two
  bounded attempts. Interesting descriptors are checked for changes between polls.
- **Le Mans Ultimate:** native `LMU_Data` mapping, the known 324,820-byte ABI for
  versions 1.2.x–1.4.x. Current builds include this interface; the legacy external
  shared-memory plugin is not required. If no source appears, check the game's
  **Enable Plugins** setting and restart the game. This helper does not change
  that setting. Future game versions report `unsupported` until their layout is
  verified. Player telemetry is selected by the player index, and scoring is
  matched by vehicle ID, never by assuming the arrays share their order.

LMU has no SDK begin/end version counters. Its SDK lock requires writing shared
synchronization data. To keep this reader read-only and avoid delaying the game,
the helper instead checks the existing lock's busy marker, takes two bounded
copies, and compares the used data and clock witnesses. Changing snapshots are
discarded without spinning. When the lock mapping is absent, the double-copy
check still applies. This is conservative best-effort coherence, not an atomic
snapshot guarantee. Only the player telemetry prefix and bounded scoring data are
copied; pointers, paths, names, scoring text and other cars' telemetry are never
dereferenced or published.

## JSON-lines contract, version 1

Each stdout line is one JSON object, below the 8 KiB output bound:

```json
{"schemaVersion":1,"game":"iracing","source":"iracing-sdk","status":"disconnected","connected":false,"stale":false,"testData":false,"sequence":1,"timestampMs":0,"ageMs":null,"data":{"speedMps":null,"gear":null,"rpm":null,"throttle":null,"brake":null,"clutch":null,"fuelLiters":null,"lap":null,"lapsCompleted":null,"lapTimeSeconds":null,"lastLapSeconds":null,"bestLapSeconds":null,"inPits":null,"flags":{"yellow":null,"blue":null,"green":null,"white":null,"checkered":null,"red":null,"black":null}},"reason":"mapping-not-found"}
```

The example describes the schema; it is not a live reading. `timestampMs` is UTC
Unix time in milliseconds. `ageMs` uses a monotonic clock and measures time since
the source tick last advanced. `sequence` is monotonic per source and may have
gaps between emitted records. Status is `live`, `stale`, `disconnected`, `invalid`
or `unsupported`; consumers must honor it rather than treating non-null numbers
as proof of a live connection. Missing/invalid channels are `null`, not zero.

Speed is meters/second; fuel is liters; times are seconds. Gear is -1 for reverse,
0 for neutral, positive for forward gears. Pedals use 0–1 depression. iRacing's
clutch-engagement value is inverted, and raw input channels are preferred when
published. Fuel is emitted as liters only when iRacing's unit metadata says
liters; an energy or ambiguous unit is not mislabeled. LMU velocity is the vector
magnitude. Lap timers or best/last laps with invalid sentinels remain null.

iRacing flags are decoded from SDK `SessionFlags`. LMU currently reports global
green phase, full-course/player yellow and player blue; red, white, black and
checkered remain null because ending or pausing a session does not prove those
flags were displayed. No driver names, IDs, credentials or paths appear in stdout.

## Build and verification

The native parent project includes this directory. It can also be built alone:

```powershell
cmake -S native/telemetry -B .cache/telemetry-build -A x64
cmake --build .cache/telemetry-build --config Release
ctest --test-dir .cache/telemetry-build -C Release --output-on-failure
```

The parser tests use hand-defined ABI fixtures, malformed offsets/counts,
nonfinite channels, changing snapshots, source freezes, and a deterministic
metadata mutation set. The process test starts a separate fixture writer and
the real helper and verifies values, rate bounds, stale/disconnected status,
invalid CLI rejection and shutdown. No running game is touched by these tests.

For desktop integration tests only:

```powershell
opengrid-telemetry-fixture.exe --namespace unique-test-id --duration-ms 6000 --freeze-after-ms 500
OpenGridXR-Telemetry.exe --game auto --rate 20 --test-namespace unique-test-id --duration-ms 2200
```

The fixture writer creates only `Local\OpenGridXR-Test-<namespace>-*` mappings
and refuses existing names. Namespace tokens are restricted to 1–64 ASCII
letters, digits and hyphens. Every helper record using a test namespace includes
`testData:true`. The fixture executable is a test artifact and must not be
packaged with the application. There is no synthetic-data fallback in normal mode.

Validation performed here covers byte fixtures and local process integration.
Actual running-game telemetry has not been validated on this development machine.
The helper makes no anti-cheat, headset, online-session or certified-compatibility
claim.
