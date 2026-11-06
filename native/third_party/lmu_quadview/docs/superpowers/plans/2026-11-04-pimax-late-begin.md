# Pimax empty submissions: late BeginFrame

Evidence: supplied `quad-20261104-162510-ae9ee719.log`, particularly the first complete frame after eye swapchain creation. Engine hooks start successfully; recommendations, native eye identities and swapchains are valid. The application locates views, acquires/releases eye images, then calls Begin and End. Captures remain 0/0 and quad projections remain zero. The user reports remaining in the Pimax environment.

Root cause reproduced: `xr_frame_views` requires an already begun frame, so rendering before Begin cannot capture. The original Begin also clears captures, so merely relaxing that predicate is insufficient.

Correction: for runtime-settings quad rendering only, a successful pending Wait plus same-time valid Locate permits the engine view accessor to perform the real Begin early. Cache its result until the application's Begin acknowledges it without resetting captures or consuming a subsequent pipelined Wait. Refuse End until acknowledgment. Failed and discarded results are returned unchanged; no retries from repeated render calls. Track all Begin calls in flight to avoid concurrent duplicate calls.

Khronos specifies Begin before rendering and prohibits throttling/synchronization in Begin: https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrBeginFrame.html

- [x] New regression fails on original code: render views unavailable after Wait/Locate.
- [x] Targeted regression passes with one runtime Begin and four submitted views.
- [x] Cover failed/discarded Begin, app acknowledgment, pipelined Wait, stale Locate, shouldRender=false, non-runtime mode and Begin in flight.
- [x] Add real D3D outer-copy coverage to runtime-settings XR test cases.
- [x] Full native suite passed (mod/build/pimax-late-begin-native.log); release package is prepared separately after committing.
- [ ] Pimax headset retest (requires reporter's hardware).

The occasional mismatched Locate/Wait timestamps in the log remain rejected. This fix is not permission to submit stale poses or unrendered eyes. Release publication remains paused at the user's request.
