# OpenGrid XR first implementation

Approved direction: fully open source and free Windows simracing toolkit, iRacing and Le Mans Ultimate first; driving and streaming are equal priorities. Use a dark charcoal and violet desktop interface. Digital signing is handled externally by the user.

## Product
OpenGrid XR / THE SIM RACING XR TOOLKIT / by Dan Suzuki. Dark charcoal, violet #7059dd, Archivo headings, Inter text, DM Mono measurements. Own navigation, own geometric preview and helmet placeholder. Real helmet assets will be authored later in Blender. No invented hardware metrics. All features free. No expiry, paywall or license activation.

## Accepted behavior
- Per-game named profiles, local save, import/export, backups and restore.
- Independent rectangular eye/FOV crop, render scale, and freeform helmet-opening contours per eye with linked editing, padding and presets.
- Driving uses the contour only when enabled. Streaming preserves the whole stream rectangle plus stabilization reserves, so a restrictive helmet mask is disabled in streaming mode in the first implementation. Explain the tradeoff; more selective union is future refinement.
- Native OpenXR layer handles projection crop and corresponding resolution recommendations, custom visibility mask if supported by the game, and per-game selection. Unsupported runtime/view modes pass through with clear status. Require session restart for allocation-affecting changes.
- Streaming capture/stabilization derives from horizonlock-vr: eye choice, smoothing, horizon lock, crop reserves, real render expansion, clean native output. No Director, natural headmotion, gaze, telemetry framing or build expiry. OBS can capture the clean output; direct plugin is a follow-on if not feasible to package in this first delivery.
- Helmet appearance is a locally drawn original fit guide, clearly labelled preview, until original assets are produced; do not claim an implemented production in-headset helmet.
- Optional VectorXR remains a separate MPL component. Native integration must be explicitly opt-in; never silently overwrite the user's existing config/layer registration.

## Architecture and choices
React + Vite + Electron control app, narrow preload IPC, Node file persistence. C++ native OpenXR runtime/clean-output component. Reuse only relevant GPL Horizonlock sources with attribution; project GPL-2.0, optional VectorXR separate MPL notices. Source references are read-only. The workspace is empty and not a Git repository, so work in place and initialize an own development branch. No copying old apps wholesale.

## Shared JSON contract (version 1)
Document: {version:1,selectedProfileId:string,profiles:Profile[]}.
Profile: {id,name,game:'iracing'|'lmu',mode:'driving'|'streaming',view:{crop:{left,right,top,bottom},renderScale},helmet:{enabled,style:'formula'|'gt'|'open',opacity,scale,offsetX,offsetY,visorTint},mask:{enabled,linked,padding,name,left:Point[],right:Point[]},stream:{eye:0|1,smoothing,horizonLock,reserve:{left,right,top,bottom},extraLeft,extraRight,offsetX,offsetY,width,height,fps,mirror}}.
All percentages are ordinary percentage values; contour Point={x,y} normalized 0..1, simple clockwise polygon in image coordinates, 3..64 points, area >=0.04. Crop each edge 0..40, opposing sum <=70. renderScale 50..200. Padding 0..10% conservatively expands contour. Stream reserve 0..30 each, extra sides 0..30, offsets -100..100, smoothing/lock 0..100, output 640..7680 by 360..4320, fps 30/60/90/120. Defaults are neutral in runtime (crop0, mask off, renderScale100). No actual helmet rendering is enabled by preview configuration.

## Desktop contract
window.openGrid methods: load() -> {document,revision,runtime:{available,registered,active,message},backups:[]}; save(document,revision) -> same; importProfile() -> Profile|null; exportProfile(profile) -> boolean; restoreBackup(id,revision) -> same; runtimeStatus() -> runtime; installRuntime() -> runtime; removeRuntime() -> runtime; launchOutput(profile) -> {started,message}; openDataFolder() -> void. Browser fallback local storage clearly identified as preview mode. Saves use atomic replacement, stale revision checks, exact previous-file backup and full validation. Profile import validates all nested data, rejects malformed/nonfinite/out-of-range/self-crossing contour inputs. User files only under the app's own data directory. Installation/registration only from explicit UI action.

## Verification
Node tests exercise real geometry, serialization, corrupt imports, stale saves, backup recovery and runtime config conversion. Native tests exercise math and layer behavior with synthetic runtime; build DLL/exe. UI smoke test covers switching games/modes, contour editing, save/reload, import/export and clean-output launch with isolated data. Inspect actual screenshots. Headset validation in iRacing/LMU remains explicitly required; do not report performance gains or compatibility before measurements.
