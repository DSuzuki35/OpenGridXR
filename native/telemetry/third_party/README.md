# Telemetry interface provenance

## iRacing

Primary SDK discussion: [iRacing SDK](https://forums.iracing.com/discussion/62/iracing-sdk)
(member login required). The SDK-authored header defines the public named mapping,
112-byte header, 144-byte variable descriptor, typed fields and buffer tick
protocol used for this independent read-only implementation.

Public redistribution inspected: [vipoo/irsdk](https://github.com/vipoo/irsdk),
pinned revision `9aff1bd30d8fadff646058d7cbf581817faf4b8b`, files
`irsdk_defines.h` and `irsdk_utils.cpp`. These contain iRacing's original source
and copyright notice. The SDK's BSD 3-clause terms are preserved in
`iracing/LICENSE`. The client implementation is not linked or bundled; no SDK
broadcast/control APIs are used. The decoder implements a smaller bounded
read-only path and skips the session YAML entirely.

## Le Mans Ultimate

Studio 397's official [v1.2 release notes](https://guide.lemansultimate.com/hc/en-gb/articles/14556121957775-V1-2-Update-2)
announce the shared-memory header in the game's support folder.
The [v1.3 announcement](https://lemansultimate.com/le-mans-ultimate-releases-v1-3-update-with-final-elms-content-performance-updates/)
confirms added shared-memory telemetry channels.
The [v1.4.2 notes](https://guide.lemansultimate.com/hc/en-gb/articles/17713037697807-V1-4-2-Update-1-4-Patch-2)
include shared-memory threading fixes.

Primary installed SDK path:
`Support/SharedMemoryInterface/{SharedMemoryInterface.hpp,InternalsPlugin.hpp,PluginObjects.hpp}`.
Studio 397's header redistribution/modification terms are restricted. No such
header or implementation is copied into this repository or required to build it.

Interoperable field layout reference: [TinyPedal/pyLMUSharedMemory](https://github.com/TinyPedal/pyLMUSharedMemory),
pinned revision `6a8dd8311d76291d602e020f059b8baae3d518a5`, `lmu_data.py` and
`lmu_enum.py`. This independently published Python mapping is MIT licensed,
copyright Tony Whitley and Xiang; its notice is preserved in
`pylmusharedmemory/LICENSE`. No Python dependency ships with the helper.

The small C++ decoder uses the interface's numeric field offsets and sizes,
verified by evaluating the pinned mapping with ctypes on Windows:

| ABI component | Byte offset / size |
| --- | --- |
| Complete mapping | 324820 bytes |
| Game version | 64 |
| Scoring block | 1632 |
| Scoring info | 548 bytes |
| Vehicle scoring array | 2192; stride 584; maximum 104 |
| Telemetry metadata | 128464; count/index/presence at +0/+1/+2 |
| Vehicle telemetry array | 128468; stride 1888; maximum 104 |
| Player velocity | telemetry record +184, +192, +200 |
| Gear / RPM | telemetry record +352 / +356 |
| Raw throttle / brake / clutch | +388 / +396 / +412 |
| Fuel liters | telemetry record +524 |

The test fixtures use these independent literal ABI facts. Native LMU's layout
is different from the legacy external plugin's buffers; those buffers are not
read, installed, or claimed as a fallback.
