# Contributing

OpenGrid XR is free and open source. Contributions should improve driving, streaming or accessibility without adding paid feature gates, advertising, account requirements or background tracking.

## Local development

Use Windows x64, Node.js 24 or newer, Visual Studio 2022 Desktop development with C++, a Windows SDK, and CMake 3.24 or newer. Run `npm ci`, `npm test`, `npm run build`, then `./scripts/build-native.ps1` from PowerShell. `npm start` opens the desktop application. `npm run test:desktop` uses isolated data and does not register the runtime layer.

Keep each change focused. Explain the observable behavior, its resource cost and the checks performed. Run tests appropriate to the changed component. New GPU paths need pixel/state/ownership checks; passing a synthetic runtime does not establish compatibility with a real headset or game.

Local work notes, handoffs and task lists belong in the ignored `.local/` directory. Public documentation describes usable features, constraints and reproducible procedures.

## Installer releases

`npm run package:installer` produces the Windows NSIS installer, blockmap and `latest.yml` under `release/<version>/`. Build native components first. Installed copies use this metadata to find stable releases; unpacked builds cannot self-update. Publish all three files together on the same GitHub Release. Never replace assets for an already published version.

The release workflow builds a draft from a `v<package-version>` tag. Review the installer and metadata before publishing that draft as a stable release. Drafts and prereleases are not offered by the application. The build is unsigned unless signing is configured externally. The updater checks the download hash from the HTTPS release metadata; no publisher certificate is configured in the unsigned build.

## Performance rules

- Do not block the game frame loop on a browser, IPC request, disk access or CPU image conversion.
- Bound queues, dimensions, source counts, frame rates and timeouts. Prefer dropping an overlay frame over delaying a sim frame.
- Disabled features should release their resources. Idle sources should not be redrawn at headset refresh rate.
- Preserve application graphics state and return unsupported paths safely to the original runtime.
- Report measured results with the game, GPU, headset, runtime, resolution and settings. Avoid unsupported FPS or latency claims.

## Source and assets

Submit code you have permission to distribute. Preserve upstream notices and record dependency versions and local changes in `THIRD-PARTY-NOTICES.md` and the dependency directory. Original helmet models and textures should include their editable source. Do not submit extracted commercial artwork, credentials, personal profiles or game SDK headers that prohibit redistribution.

## Reporting issues

Use the repository's issue tracker. Include the OpenGrid version, Windows version, GPU/driver, game, OpenXR runtime, enabled features and a minimal reproduction. Remove tokens, private dashboard URLs and personal data before attaching profiles or logs. Reports may use synthetic data where possible.
