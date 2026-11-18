# Does LMU publish cockpit head movement?

The test needs the rFactor2 shared-memory plugin's graphics output. With LMU closed, check the `rFactor2SharedMemoryMapPlugin64.dll` entry in `UserData/player/CustomPluginVariables.JSON`. If `UnsubscribedBuffersMask` is 160, record the original value and set it to 128 for the test. This enables graphics updates while leaving weather disabled. For other values, clear only the graphics bit (32). Restore your previous value after testing. The script makes no configuration changes.

Enter the player cockpit in LMU VR and run the [diagnostic script](https://github.com/DSuzuki35/OpenGridXR/blob/v1.1.0/scripts/probe-lmu-cockpit-camera.py) from the repository:

```powershell
py scripts/probe-lmu-cockpit-camera.py --seconds 20 --out camera-off.jsonl
```

1. With in-game head movement off, stay parked and physically turn your head, then lean a little. This shows whether the graphics camera includes headset tracking.
2. Enable in-game head movement, keep your head still and brake on a straight. Record to `camera-on.jsonl`. Repeat the same braking with movement off in a third file. Compare the camera position and rotation in car coordinates.

The script opens existing mappings read-only. Each sample records elapsed wall time, the graphics update counter, vehicle simulation time and the camera pose relative to the player car. Position uses LMU metres and rotation is a row-major 3×3 matrix. Graphics and vehicle physics have independent update clocks, so differences during fast movement can include timing skew. An in-headset comparison is still necessary before using these data to position a dashboard.

`unknown` means a sample was busy, stale, invalid or outside the player cockpit. `unavailable` identifies a missing or inaccessible mapping. Exit code 2 means there were no usable samples. The graphics buffer is optional and unsubscribed by default in the [rFactor2 shared-memory plugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin#unsubscribing-from-the-buffer-updatdes). Native LMU vehicle telemetry uses the [installed game's shared-memory interface](../native/telemetry/third_party/README.md).
