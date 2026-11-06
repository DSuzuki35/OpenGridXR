# Changelog

## 0.6.0

### Helmet and stream

- Fixed the grid-like artifacts cutting through visor lettering and custom graphics in VR and Clean Output.
- Lettering and graphics now use resolution-aware intermediate textures, including the 4K detail setting. Existing visor positions and helmet shapes are preserved.
- Fit complete letters before clipping them to the sunstrip, preserving rounded bottoms and descenders in lowered text.
- Stabilization reserve and horizontal/vertical framing preview immediately while adjusting the controls in Stream. Save keeps the changes; discarding edits or switching profiles restores saved framing. More reserve crops the game image further and increases zoom; the stream helmet keeps its independent size and position.

### Community fixes — Markus Stahl

The review-fixes patch contributed by **Markus Stahl** was reviewed, adapted and integrated for this release:

- Closing with unsaved edits offers a confirmation instead of silently refusing to close.
- Profile backups are limited to 100; abandoned save locks recover without taking ownership from a live writer.
- The start script selects the build matching `package.json`; recenter input uses scan codes.
- Reduced repeated editing calculations, hidden-page polling, runtime DLL hashing and LMU installation discovery.
- Swapchain image waits release the global layer lock so unrelated calls can proceed. Added lifetime reservations protect the swapchain, session and instance during the wait, including teardown and callback reentry.
- Hardened the administrator helper's DLL search and restricted development-only local server connections.
- Pinned GitHub Actions and separated release building from the job with publishing permissions.
- Removed unused imports and dead code; added regression coverage for save/lock handling.

The original patch was adapted with active-writer protection, cache invalidation and retry handling. The LMU adapter's existing bounded waits and ownership guards are retained.

### Updating

Install 0.6.0, close the sims and Clean Output, then update the runtime layer in Setup. Save your helmet profile once to regenerate its artwork with the corrected renderer. Start the sim and Clean Output again.

Earlier release notes are available on [GitHub Releases](https://github.com/DSuzuki35/OpenGridXR/releases).
