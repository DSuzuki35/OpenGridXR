# Telemetry interface

`OpenGridXR-Telemetry.exe` is a standalone read-only reader. It does not modify game configuration or write into game telemetry mappings. The desktop app starts it only while a telemetry consumer requests data; leaving the telemetry view allows it to stop automatically.

The executable lives in `resources/native` inside the packaged application, or in `native/build/Release` after a source build. Run the following commands from that directory.

```powershell
./OpenGridXR-Telemetry.exe --game iracing --rate 20
./OpenGridXR-Telemetry.exe --game lmu --rate 20
./OpenGridXR-Telemetry.exe --game auto --rate 20
```

Standard output is UTF-8 JSON Lines. Each line has `schemaVersion: 1`, `game`, `source`, `status`, `connected`, `stale`, `sequence`, `timestampMs`, `ageMs`, `data`, `reason`, and `testData`. Automatic mode emits independent records for each game. Update rate is capped at 30 Hz; unavailable sources reconnect more slowly.

`status` is one of `live`, `stale`, `disconnected`, `invalid`, or `unsupported`. Missing data is `null`, never zero-filled. Consumers must honor freshness and withdraw live readouts after silence. `sequence` identifies updates; `timestampMs` uses Unix milliseconds, and `ageMs` measures source age when available. Production data has `testData: false`; isolated test fixtures are explicitly marked.

`data` contains speed in meters/second (`speedMps`), `gear` (-1 reverse, 0 neutral), `rpm`, normalized `throttle`/`brake`/`clutch`, `fuelLiters`, `lap`, `lapsCompleted`, `lapTimeSeconds`, `lastLapSeconds`, `bestLapSeconds`, `inPits`, and a `flags` object with `yellow`, `blue`, `green`, `white`, `checkered`, `red`, and `black`. A flag the source does not expose is null.

No driver names, account IDs, full session metadata or network service are exposed by this interface. Local dashboard integrations can consume the process pipe without opening a listening port. Reading a URL dashboard in a VR overlay is independent of this telemetry interface.

Optional acceleration channels are `lateralAccelMps2` (positive left), `longitudinalAccelMps2` (positive forward) and `verticalAccelMps2` (positive up), all in meters per second squared. Unavailable or invalid channels are null. Helmet vehicle movement reads the same native sources at up to 20 Hz only when enabled and fades out when samples stop advancing; the desktop telemetry page need not remain open.

Optional benchmark context is additive to schema 1; older helpers may omit it. New helpers emit unavailable values as `null`. These fields share the record's freshness and must not be treated as live when `stale` is true.

| Field | Meaning and availability |
| --- | --- |
| `airTempC`, `trackTempC` | Celsius. iRacing's unit-checked `AirTemp` and `TrackTempCrew` (fallback `TrackTemp`); LMU's scoring ambient/track temperatures. |
| `rainIntensity` | Simulator-reported fraction 0–1: iRacing `Precipitation`, LMU `mRaining`. Not millimetres/hour. |
| `cloudCover` | Sky category string, **not a coverage percentage**. Common values: `clear`, `partly-cloudy`, `mostly-cloudy`, `overcast`. LMU also exposes `light-clouds`, `cloudy-drizzle`, `cloudy-light-rain`, `overcast-light-rain`, `overcast-rain`, `overcast-heavy-rain`, `overcast-storm`. |
| `trackWetness` | LMU average main-path wetness fraction 0–1; null for iRacing. |
| `trackWetnessState` | iRacing's category: `dry`, `mostly-dry`, `very-lightly-wet`, `lightly-wet`, `moderately-wet`, `very-wet`, `extremely-wet`. Unknown and LMU remain null. Categories are not converted into invented fractions. |
| `sessionTimeSeconds` | Simulator session elapsed time. |
| `timeOfDaySeconds` | Simulator clock, seconds since midnight (0–86400). iRacing `SessionTimeOfDay`, LMU `mTimeOfDay`. Does not by itself establish daylight. |
| `solarAltitudeRadians` | iRacing's measured sun elevation above the horizon; null for LMU. No sunrise/sunset is guessed from clock time. |
| `trackName` | Bounded printable track identifier/name. No driver or account details are emitted. |
| `sessionId` | iRacing `SessionUniqueID` plus `SessionNum`, formatted `iracing-N-N`. LMU is null because a verified unique event ID is unavailable. |
| `carsNearby` | Count of other available cars within the declared distance threshold. Not a rendering or visibility count. |
| `carsNearbyRadiusMeters` | 100 when the nearby calculation is available; otherwise null. |
| `carsNearbyMethod` | `world-distance` for LMU, `track-distance` for iRacing; otherwise null. |
| `carsVisible` | Always null with the current supported SDKs. No verified count of actually rendered visible cars is exposed. |

LMU nearby counts use three-dimensional Euclidean distances between scoring world positions, excluding the player and slots controlled by nobody. iRacing uses the shorter wrapped distance around the lap from `CarIdxLapDistPct` and `WeekendInfo.TrackLength`, excluding the player and `CarIdxTrackSurface = NotInWorld`. This is an along-track approximation: adjacent track sections, pit lanes, bridges and lateral separation can differ from physical distance. Neither method tests view frustum, occlusion, mirrors, render limits or whether a car is drawn. In-world cars with invalid positions make the whole count unavailable rather than silently undercounting. Cars omitted by the simulator's data feed cannot be counted.

LMU scoring context is withheld if its clock differs from player physics by more than two seconds. Its newer sky category and time-of-day expansion fields are read only for the verified 1.4.2-or-later supported layout (versions below 1.5); older layouts return null for those fields. Track names accept printable ASCII only; unsupported encodings remain unavailable. iRacing reads only two bounded `WeekendInfo` scalars from session YAML (`TrackName`, `TrackLength`) and discards all other metadata.

Field provenance and numeric offsets are documented in [the native telemetry references](../native/telemetry/third_party/README.md). Simulator data is read-only; fixture coverage does not replace physical-game validation.
