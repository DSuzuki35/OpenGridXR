# Benchmark overlay and reports contract (2026-11-02)

User requests an in-headset benchmark overlay similar to VectorXR, Dan Suzuki UI-kit styling, Minimal/Detailed templates, separately bindable joystick buttons for visibility and recording, manual start/stop around a lap, a report, and clearer diagnostics. Manual lap marking confirmed; no lap telemetry integration. Existing autonomy approval applies.

Design: native adapter publishes bounded per-frame samples through a per-process local shared-memory mapping and submits a separate head-locked OpenXR QUAD overlay. Launcher stays running, polls samples and joystick, draws the overlay bitmap at 5 Hz outside the game, and records/report-generates outside the render thread. Feature opt-in via LMU_QV_BENCHMARK=1; diagnostics-off disables it. No PowerShell dependency. All source C++17 or .NET4.8 C#5. No external runtime libraries/fonts required; use installed Archivo/Inter/DM Mono with Segoe UI/Consolas fallbacks.

Native/managed shared ABI v1, little-endian, natural offsets explicitly checked:
Mapping name Local\LMUQuadView.Benchmark.<decimal PID>; total size=1900800 bytes.
Header 256 bytes:
0 u32 magic=0x51424D4C; 4 u32 version=1; 8 u32 size=1900800; 12 u32 PID;
16 i64 QPC frequency; 24 i64 last published sample counter; 32 i64 heartbeat QPC;
40 i32 config sequence (launcher writes odd/change/even); 44 i32 overlayVisible;
48 i32 template (0 Minimal,1 Detailed); 52 i32 recording (display only);
56 float overlayX metres default0;60 float overlayY default-0.25;64 float distance default1.2;68 float width default0.65;
72 i32 bitmap sequence (launcher odd/change/even);76 i32 bitmapWidth;80 i32 bitmapHeight;
84 i32 native status (0 healthy,1 initializing,2 no valid projection,3 overlay failed,4 frame failure);
88 i32 running session;92 i32 latest XrResult;96 i32 last quad scene projection count;
100 u32 GPU failed/disjoint count;104 reserved;
108 u32 outer0Width;112 outer0Height;116 outer1Width;120 outer1Height;124 focusWidth;128 focusHeight;
132 i32 diagnostic reason (0 none,1 invalid/stale capture,2 invalid eye/subimage,3 invalid pose,4 overlay creation/update failure).
Remainder reserved zero.

Ring begins at offset256, 4096 slots, stride80. Counter starts1; slot=(counter-1)%4096.
Each slot:0 i64 commitCounter (negative while writing, positive final via InterlockedExchange64);8 i64 QPC timestamp;16 u64 XR frame id;24 double scene interval ms (NaN if no consecutive scene);32 double app wall ms (BeginFrame through pre-submit, including waits, not CPU utilization);40 double provider EndFrame wall ms;48 double GPU span ms (NaN unless NEW result);56 u64 GPU source frame id;64 u32 flags(scene=1,shouldRender=2,quadSubmitted=4,gpuValid=8);68 i32 XrResult;72 reserved8.
Native fills slot then publishes global counter with interlocked operations; reader must verify commit equals wanted before/after copy. Count overwritten samples in reports; never silently present loss-free stats. GPU samples sparse/nonblocking, source frame id retained; GPU span excludes downstream provider/compositor and can include idle gaps, not hardware utilization. No invented full GPU frametime.

Bitmap starts offset327936; max768x512 BGRA8 premultiplied/opaque pixels (1572864 bytes). Launcher uses bounded sequence and header dimensions; native copies only stable bitmap versions, never spins/waits for writer. Native can convert BGRA to RGBA if runtime format requires. Submit XrCompositionLayerQuad in VIEW space with its own swapchain and no game render-state changes. Acquire/wait timeout0/release ownership; failure drops only overlay, never game. Hide overlay immediately by config; sampling/recording continues. Avoid adding XR entry points to mandatory Dispatch load; optional lookup only when feature enabled. Zero per-frame IO/sorting/unbounded allocations; GPU query DONOTFLUSH, no blocking waits. Clean resources on teardown; never hold IPC locks across runtime calls.

Managed public contract (namespace LmuQuadViewLauncher):
BenchmarkController(string root) : IDisposable
Properties bool Connected, Recording, OverlayVisible {get;set;}; int Template {get;set;} (0/1); float OverlayX,OverlayY,OverlayDistance,OverlayWidth {get;set;}; string Status, LastReportPath; BenchmarkSnapshot Snapshot {get;};
Methods void Tick() [UI timer50ms, discover matching running LMU mapping when unattached], void ToggleRecording(), void ToggleOverlay(), void OpenLastReport(); Bitmap RenderPreview() [caller disposes].
Controller handles mapping/report/bitmap and catches transient connect/IO errors into Status. UI consumes Snapshot optionally, not required. No launcher/game startup from constructor. Tick updates max5Hz bitmap, no blocking report build on UI thread; finalize report in background with bounded samples cap and clear truncation. Capture stops on lost process/connection, partial report, periodically flush CSV checkpoints. Report self-contained HTML+CSV+JSON under root/benchmarks with app FPS/mean/p50/p95/p99/max frame interval, app wall, provider wait, sparse GPU statistics, graph, dropped counts, scene/menu counts and measurement limits. HTML escapes user strings and no remote resources. Never label software metrics CPU/GPU utilization or imply total compositor GPU costs. Captured raw data exclude user paths and device serial numbers.

Joystick: DirectInput8 background/nonexclusive, GUID-instance keyed button0..127; bind via listen for rising edge after current held buttons released; disconnect suppresses toggles and reconnect seeds state; no forced focus or input injection. Distinct bindings stored in launcher XML as device GUID/button index. Keyboard fallback can be Ctrl+Alt+F10 visibility/Ctrl+Alt+F11 recording (foreground LMU/launcher only). Tests use injectable button source and confirm edge/debounce/device/removal semantics. Launcher page owns joystick polling/controller lifetime; do not interfere existing Ctrl+Alt+F9 profiling.

UI: new Benchmark page, enable at next launch checkbox, template choice, show/hide & record/stop buttons, assign/clear joystick bindings, position/scale inputs, live preview and status/report button. Layout scales and scrolls like existing pages. Native env opt-in only if DiagnosticsEnabled and BenchmarkEnabled. Theme colors from local website ui-kit.css: carbon#0f0f12,graphite#1a1b1f,border#32343c,text#f4f4f6,muted#9da0aa,accent#7059dd,accentText#a79bf0,red#f87171,green#22c55e. Rounded panels, mono numbers, quiet labels.

Diagnostics: distinguish no connection, stopped heartbeat, rendered but zero submitted projections, invalid resource identity, stale/invalid capture, invalid poses, overlay-only failure, ring loss, GPU unavailable; next steps explain which actual log is useful. Don't diagnose unobservable headset/compositor failure as confirmed.

Verification: shared ABI offsets/size+ring stability/overrun, fixture telemetry and known percentiles/report escaping, disconnect partialsave, joystick transitions, fake-runtime overlay lifecycle/errors, sampler query/state lifecycle, native regression suite and GUI layout/screenshot tests. No headset/joystick hardware claims without real tests. Package candidate including preceding motion/crop changes, no GitHub push unless requested.
