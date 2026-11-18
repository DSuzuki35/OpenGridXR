# OpenGridXR-Layers

Windows-only fixed-purpose OpenXR API layer registration helper. The desktop app
runs normally; only a single requested HKLM change asks Windows for elevation.

```
OpenGridXR-Layers.exe --inventory
OpenGridXR-Layers.exe --set HKCU|HKLM 64|32 implicit|explicit manifest.json expectedUInt32 desiredUInt32
OpenGridXR-Layers.exe --elevate-set HKLM 64|32 implicit|explicit manifest.json expectedUInt32 desiredUInt32
```

`manifest.json` must be an absolute drive-qualified local JSON path (maximum 8192
UTF-16 code units). Quotes, control characters, UNC/device paths, alternate data
streams, wildcards, empty or dot components, and trailing component spaces/dots
are rejected. The helper does not read, load, or execute the manifest. It uses the
path solely as the name of an existing registry value. No command, arbitrary key,
input file, or output file option exists.

Inventory prints one UTF-8 JSON object with `entries` and `errors` arrays. Entries
have string fields `scope`, `view`, `kind`, `manifest`, `type`, and numeric/null
`value`. Valid `REG_DWORD` values use `type: "DWORD"`; malformed DWORDs and other
types have `value: null`. Non-DWORD names use `REG_SZ`, `REG_BINARY`, etc. Missing
keys are normal. Access/enumeration errors are reported by scope/view/kind while
other keys remain visible. Inventory includes malformed names for diagnosis;
mutation validation is stricter.

HKCU\\SOFTWARE is shared across WOW64 views. Raw inventory therefore contains
both logical view aliases; the desktop service canonicalizes their identity and
shows one shared registration. HKLM entries retain separate 32/64-bit identities.

The only keys opened are the eight combinations of HKCU/HKLM, 64/32-bit view, and
`SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` or `Explicit`. Mutation opens an
existing key in a Windows registry transaction, requires an existing four-byte
DWORD matching `expectedUInt32`, writes the requested DWORD, commits, then opens a
fresh nontransactional handle for readback. Failure never falls back to an
unconditional write. Uncommitted transactions roll back when their handle closes.
Both integers accept decimal uint32 values, including nonzero values above one,
so restore can preserve the exact original value. No creation/deletion is exposed.

The helper checks running supported games and Clean Output before starting a
transaction and immediately before committing. Snapshot failures block mutation.
This minimizes the game-start race but cannot atomically lock Windows process
creation together with a registry transaction.

`--elevate-set` rejects HKCU before opening UAC. It builds only a validated `--set
HKLM` command for its own absolute executable path, uses ShellExecuteExW `runas`,
waits for the child and forwards its exit status. The elevated child validates
arguments and checks processes again. No shell or external script is launched.

Exit status is zero for success. Common failures: 87 invalid arguments, 2 missing
value/key, 13 non-DWORD/malformed value, 1306 stale expected value, 1117 readback
mismatch, 170 running supported process, and 1223 UAC cancellation. Other Win32
errors (including transaction conflicts and access denial) are preserved. A
readback failure can occur after a committed write; callers must retain their
backup and refresh inventory rather than assume no change.

Build targets: `OpenGridXR-Layers` and `opengrid-layer-registry-tests`. CTest names:
`native-layer-registry` and `native-layer-registry-cli`. Tests use an injected
registry for writes; the real CLI tests only reject invalid commands and enumerate.
They never request UAC or modify real layer registrations.

API references: [RegOpenKeyTransactedW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeytransactedw),
[CreateTransaction](https://learn.microsoft.com/en-us/windows/win32/api/ktmw32/nf-ktmw32-createtransaction),
[ShellExecuteExW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexw).
