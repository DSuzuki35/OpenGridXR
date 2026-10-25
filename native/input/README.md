# Peripheral input helper

`OpenGridXR-Input.exe` reads DirectInput8 game-controller buttons and POV hats. It uses a hidden top-level window, background/nonexclusive access, `DIJOYSTATE2`, up to128 buttons and four hats per device, and at most64 attached devices. Device IDs use the Windows DirectInput instance GUID, never enumeration order. Polling is capped at60Hz; device enumeration runs once a second. Unavailable acquisition retries are bounded. Axes, input injection, force feedback and device configuration writes are not implemented.

Only device-list changes and new presses are emitted as JSON lines. Startup, input loss, reacquisition and detach reset the edge baseline, so held controls do not activate an action. Hat directions0..7 run clockwise from up; neutral releases the current direction without emitting a press. Moving directly to a different direction emits its new press once.

The parent provides a stdin pipe. EOF or `{"op":"quit"}` ends capture and releases devices. `desktop/input-host.cjs` starts the process only while requested, validates bounded records, ignores retiring/stale children, and retries unexpected failures with exponential backoff. Test mode blocks real capture unless the explicit synthetic fixture is requested.

`--self-test` runs pure edge tests without loading DirectInput or enumerating hardware. `--fixture` emits one synthetic device, then emits button127 when it receives `{"op":"fixture-press"}`; it never accesses hardware. `tests/input-host.test.mjs` covers the wrapper and the actual fixture pipe/EOF behavior. Physical wheel/buttonbox operation in the background remains unvalidated on this development machine.

API behavior follows Microsoft's [cooperative-level documentation](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417921(v=vs.85)) and [POV/button-state definitions](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416627(v=vs.85)). No upstream controller library or proprietary SDK is bundled.
