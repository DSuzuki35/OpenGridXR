# Contributing

OpenGrid XR is source-available for noncommercial use under PolyForm Noncommercial 1.0.0, with separately licensed dependencies. Contributions should improve driving, streaming or accessibility without adding paid feature gates, advertising, account requirements or background tracking.

## Local development

Use Windows x64, Node.js 24 or newer, Visual Studio 2022 Desktop development with C++, a Windows SDK with `fxc`, CMake 3.24 or newer, and Python 3 for the bundled QVFR dispatch generator. Run `npm ci`, `npm test`, `npm run build`, then `./scripts/build-native.ps1` from PowerShell. `npm start` opens the desktop application. `npm run test:desktop` uses isolated data and does not register the runtime layer.

After a production renderer build, run `node tests/accessibility-ui.cjs` to check all 15 editor pages with axe and `node tests/modal-dialog-ui.cjs` to check keyboard focus, Escape and dialog scrolling.

Keep each change focused. Explain the observable behavior, its resource cost and the checks performed. Run tests appropriate to the changed component. New GPU paths need pixel/state/ownership checks; passing a synthetic runtime does not establish compatibility with a real headset or game.

Local work notes, handoffs and task lists belong in the ignored `.local/` directory. Public documentation describes usable features, constraints and reproducible procedures.

## Installer releases

For a signed distribution, sign the built QVFR DLL in `native/build/Release/providers/qvfr` along with the OpenGrid runtime binaries before packaging. Verify the signature of the packaged copy. Runtime installation preserves its signed bytes under the private provider filename.

`npm run package:installer` produces the Windows NSIS installer, blockmap and `latest.yml` under `release/<version>/`. Build native components first. Installed copies use this metadata to find stable releases; unpacked builds cannot self-update. Publish all three files together on the same GitHub Release. Never replace assets for an already published version.

The release workflow builds a draft from a `v<package-version>` tag. Review the installer and metadata before publishing that draft as a stable release. Drafts and prereleases are not offered by the application. The build is unsigned unless signing is configured externally. The updater checks the download hash from the HTTPS release metadata; no publisher certificate is configured in the unsigned build.

## Performance rules

- Do not block the game frame loop on a browser, IPC request, disk access or CPU image conversion.
- Bound queues, dimensions, source counts, frame rates and timeouts. Prefer dropping an overlay frame over delaying a sim frame.
- Disabled features should release their resources. Idle sources should not be redrawn at headset refresh rate.
- Preserve application graphics state and return unsupported paths safely to the original runtime.
- Report measured results with the game, GPU, headset, runtime, resolution and settings. Avoid unsupported FPS or latency claims.

## Source and assets

Submit code you have permission to distribute. Preserve upstream notices and record dependency versions and local changes in `THIRD-PARTY-NOTICES.md` and the dependency directory. Original models and textures should include their editable source. Do not submit extracted commercial artwork, credentials, personal profiles or game SDK headers that prohibit redistribution.

Submit contributions under the license applicable to the affected files: PolyForm Noncommercial 1.0.0 for original OpenGrid XR work unless otherwise stated, and the existing dependency license for changes to separately licensed code. In particular, modifications to MPL-covered VectorXR files remain MPL-2.0. See `LICENSE` and `THIRD-PARTY-NOTICES.md` for scope and notices.

Contributors retain their copyright; contributing does not assign ownership or automatically authorize relicensing. A future release under GPL or another license would require the necessary rights or explicit permission from the copyright holders of affected contributions, while respecting all dependency licenses. Earlier GPL releases retain their original license grants.

## Fixing issues

This project is provided as-is, without technical support. I will not answer DMs about it.

You are welcome to clone or fork the repository and use your favorite AI coding tool to investigate and fix an issue. Review the proposed changes, run the relevant checks, and submit a focused pull request. Include the problem, a minimal reproduction, what changed and your test results. For runtime issues, include the OpenGrid version, Windows version, GPU/driver, game, OpenXR runtime and enabled features. Remove tokens, private dashboard URLs and personal data before attaching profiles or logs.

Contributions are welcome, but a response, review or merge is not guaranteed. The issue tracker is not a technical support service.
