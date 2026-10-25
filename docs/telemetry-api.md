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
