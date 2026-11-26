# Peripheral input helper

`OpenGridXR-Input.exe` reads DirectInput8 game-controller buttons, POV hats and opt-in steering axes. It uses a hidden top-level window, background/nonexclusive access, `DIJOYSTATE2`, up to128 buttons and four hats per device, and at most64 attached devices. Device IDs use the Windows DirectInput instance GUID, never enumeration order. Polling is capped at60Hz; device enumeration runs once a second. Unavailable acquisition retries are bounded. Input injection, force feedback and hardware configuration writes are not implemented.

Device-list changes, timestamped presses/releases and fresh held-state snapshots are emitted as JSON lines. Successful device reads publish held snapshots every100ms; read failure emits an empty snapshot. Startup, input loss, reacquisition and detach reset the edge baseline, so controls already held do not activate an action. Hat directions0..7 run clockwise from up; neutral releases the current direction without emitting a press. Moving directly to a different direction emits its new press once.

Desktop precision holds renew only from hardware snapshots; records buffered for more than250ms are rejected and an unrenewed hold expires after300ms. Editor/profile/source changes, capture learning, disconnect and application exit release active holds. Ordinary toggle actions remain edge-triggered. Same-desktop left/right click actions can run while precision is held.

The parent provides a stdin pipe. EOF or `{"op":"quit"}` ends capture and releases devices. `desktop/input-host.cjs` starts the process only while requested, validates bounded records, ignores retiring/stale children, and retries unexpected failures with exponential backoff. Test mode blocks real capture unless the explicit synthetic fixture is requested.

`--self-test` runs pure edge tests without loading DirectInput or enumerating hardware. `--fixture` emits one synthetic device, then emits button127 when it receives `{"op":"fixture-press"}`; it never accesses hardware. `tests/input-host.test.mjs` covers the wrapper and the actual fixture pipe/EOF behavior. Physical wheel/buttonbox operation in the background remains unvalidated on this development machine.

API behavior follows Microsoft's [cooperative-level documentation](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417921(v=vs.85)) and [POV/button-state definitions](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416627(v=vs.85)). No upstream controller library or proprietary SDK is bundled.

## Steering axes

`desktop/steering-input.cjs` exports `createSteeringInputController({createHost})` and `steeringSettingsToken(settings)`. Give it a dedicated `createInputHost` instance so button bindings retain their existing lifecycle and output. The controller API is:

```js
controller.configure({source:'device',deviceId:'dinput:{...}',axis:'x',lockDegrees:900,invert:false},true);
controller.status({discover:true}); // {devices:[{id,name,axes}],samples:{[id]:{[axis]:normalized}},available,error}
controller.releaseDiscovery(); // panel hidden/unmounted
controller.close();
```

`source:'telemetry'` is the default. An empty device ID is an unassigned device source and produces neutral output. Supported axis names are `x`, `y`, `z`, `rx`, `ry`, `rz`, `slider0`, `slider1`. Lock is total rotation, 90–2520 degrees in whole-degree steps, default900. Increasing DirectInput values turn right: normal mapping therefore produces **negative** renderer radians. At900 degrees, normalized-1/0/+1 produces+450/0/-450 degrees; inversion reverses that sign. No steering lock or calibration value is written to the wheelbase.

Discovery lasts30seconds after the last `status({discover:true})` call. Poll only while the chooser is visible, and call `releaseDiscovery()` when it closes. An active assigned device source keeps its selected axis running after discovery ends. Both desktop and native processes enforce discovery expiry. Preview samples expire after250ms; an empty axes report or a removed device clears them immediately.

The host's optional `onAxes` callback receives `{deviceId,axes,sampledAt}`. `configureSteering` sends a fixed-schema bounded JSON line with keys in this order:

```json
{"op":"steering","enabled":true,"discover":false,"deviceId":"dinput:{01234567-89ab-cdef-0123-456789abcdef}","axis":"x","lockDegrees":900,"invert":false,"token":123}
```

The helper enumerates absolute axis objects, matches the eight `DIJOYSTATE2` offsets through `GetObjectInfo`, and reads each supported [DIPROP_RANGE](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416640(v=vs.85)) through `GetProperty`. Unsupported, unrestricted and degenerate ranges are omitted, never guessed. Values normalize around the reported range midpoint and clamp to[-1,1]. Capabilities are refreshed at most once per second while requested. Discovery emits preview samples at10Hz per device; selected-axis mode emits at20Hz. The shared steering angle updates at the regular60Hz poll rate. Devices that are neither selected nor being discovered are not polled by the dedicated steering helper.

## Native steering channel

`native/include/steering_input.hpp` provides `og::steering::Reader`. `reader.read(expectedToken)` returns `std::optional<float>` radians; callers use `.value_or(0.f)` for neutral when a configured device is unavailable. `reader.reset()` closes the view when no steering-follow slots remain. No telemetry/protocol ABI fields are shared with this channel.

The mapping is `Local\OpenGridXR.SteeringInput.v1`. Its32-byte ABI is: sequence(uint32), version(uint32=1), sampledTick(uint64, `GetTickCount64` milliseconds), radians(float32), valid(uint32), token(uint32), reserved(uint32). A bounded seqlock read rejects in-progress or changed writes, wrong versions/tokens, nonfinite angles and samples older than250ms. Token0 is reserved for inactive/telemetry selection. `steeringSettingsToken` is nonzero FNV-1a over the canonical normalized settings tuple `[source,deviceId,axis,lockDegrees,invert]`. A slot must pass the token for its own profile so it cannot consume another profile's assignment.

Only a steering-enabled helper opens the writer. Mode/configuration changes, lost acquisition, removal, and orderly shutdown invalidate its output. Unexpected process loss expires by timestamp. A new helper safely resets the sequence if a reader retained the previous mapping, including an interrupted prior write. All handles release on shutdown/reset. Normal bindings-only helpers never create or write this mapping.

`--self-test` additionally checks normalization, lock/inversion, freshness, profile tokens, disconnect/reconnect and retained-mapping restart. Tests use private mapping names. The explicit `--fixture` accepts `fixture-axis` (normalized x value), `fixture-disconnect`, and `fixture-reconnect` commands after steering configuration; it uses a private per-process mapping and never changes live steering. Physical wheel steering/background acquisition still needs manual validation with hardware.
