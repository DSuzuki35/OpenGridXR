"""Read-only LMU cockpit camera diagnostic. Usage: see docs/cockpit-camera-probe.md."""
import argparse
import ctypes
from ctypes import wintypes
from contextlib import ExitStack, contextmanager
import json
import math
import struct
import sys
import time


@contextmanager
def read_mapping(name, size):
    # mmap(..., tagname=...) can create absent sections on Windows. Open existing only.
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.OpenFileMappingW.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR)
    api.OpenFileMappingW.restype = wintypes.HANDLE
    api.MapViewOfFile.argtypes = (wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.DWORD, ctypes.c_size_t)
    api.MapViewOfFile.restype = ctypes.c_void_p
    api.UnmapViewOfFile.argtypes = (ctypes.c_void_p,)
    api.CloseHandle.argtypes = (wintypes.HANDLE,)
    read_access = 4  # FILE_MAP_READ
    handle = api.OpenFileMappingW(read_access, False, name)
    if not handle:
        raise OSError(ctypes.get_last_error(), f"Cannot open {name}")
    try:
        base = api.MapViewOfFile(handle, read_access, 0, 0, size)
        if not base:
            raise OSError(ctypes.get_last_error(), f"Cannot map {name}")
        try:
            def read(offset, length):
                if offset < 0 or length < 0 or offset + length > size:
                    raise ValueError("Mapping read outside verified layout")
                return ctypes.string_at(base + offset, length)
            yield read
        finally:
            api.UnmapViewOfFile(base)
    finally:
        api.CloseHandle(handle)


def car_snapshot(read, lock):
    if struct.unpack("<i", lock(4, 4))[0]:
        raise ValueError("LMU telemetry busy")
    version, = struct.unpack("<i", read(64, 4))
    if not 12000 <= version < 15000:
        raise ValueError("Unknown LMU telemetry version")
    count, player, driving = read(128464, 3)
    if not (driving == 1 and player < count <= 104):
        raise ValueError("No player car")
    # Official LMU_Data x64, packed to 4 bytes; see native/telemetry/third_party.
    return player, read(128468 + player * 1888, 304)


def graphics_snapshot(read):
    before = read(0, 8)
    begin, end = struct.unpack("<II", before)
    if not begin or begin != end:
        raise ValueError("Graphics buffer inactive or busy")
    data = read(8, 136)
    if read(0, 8) != before:
        raise ValueError("Graphics buffer changed during read")
    return begin, data


def rows(values):
    return [values[i:i+3] for i in (0, 3, 6)]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def checked_rotation(values):
    r = rows(values)
    for i in range(3):
        for j in range(3):
            if not math.isclose(dot(r[i], r[j]), float(i == j), abs_tol=.015):
                raise ValueError("Invalid rotation")
    det = sum(r[0][i] * (r[1][(i+1)%3]*r[2][(i+2)%3] -
                          r[1][(i+2)%3]*r[2][(i+1)%3]) for i in range(3))
    if not math.isclose(det, 1, abs_tol=.025):
        raise ValueError("Invalid rotation handedness")
    return r


def relative_pose(car, camera):
    car_id, = struct.unpack_from("<i", car)
    camera_id, mode = struct.unpack_from("<ii", camera, 128)
    if car_id < 0 or camera_id != car_id or mode != 1:
        raise ValueError("Camera is outside player cockpit")
    sim_time, = struct.unpack_from("<d", car, 12)
    cp = struct.unpack_from("<3d", car, 160)
    gp = struct.unpack_from("<3d", camera)
    if not 0 <= sim_time < 1e9 or not all(abs(x) <= 1e7 for x in cp+gp):
        raise ValueError("Invalid position or simulation clock")
    # Orientation rows map vehicle-local vectors to world. Its transpose maps back.
    axes = list(zip(*checked_rotation(struct.unpack_from("<9d", car, 232))))
    cam = checked_rotation(struct.unpack_from("<9d", camera, 24))
    delta = [a-b for a, b in zip(gp, cp)]
    return {"car_id": car_id, "sim_s": sim_time,
            "camera_in_car_m": [dot(axis, delta) for axis in axes],
            "camera_in_car_rotation": [[dot(axis, col) for col in zip(*cam)] for axis in axes]}


def run(seconds, hz, output):
    def write(data):
        print(json.dumps(data, allow_nan=False, separators=(",", ":")), file=output, flush=True)

    with ExitStack() as stack:
        try:
            car = stack.enter_context(read_mapping("LMU_Data", 324820))
            lock = stack.enter_context(read_mapping("LMU_SharedMemoryLockData", 8))
            camera = stack.enter_context(read_mapping("$rFactor2SMMP_Graphics$", 144))
        except OSError as exc:
            write({"status": "unavailable", "reason": str(exc), "camera_in_car_m": None})
            return 2
        previous = [None, None]
        changed = [None, None]
        start = time.perf_counter()
        valid = 0
        for n in range(seconds * hz):
            now = time.perf_counter()
            row = {"elapsed_s": round(now-start, 5), "camera_in_car_m": None}
            try:
                first = car_snapshot(car, lock)
                version, image = graphics_snapshot(camera)
                second = car_snapshot(car, lock)
                if first != second or struct.unpack("<i", lock(4, 4))[0]:
                    raise ValueError("Player telemetry changed during read")
                pose = relative_pose(second[1], image)
                row.update(graphics_counter=version, sim_s=pose["sim_s"])
                for index, current in enumerate((version, (pose["car_id"], pose["sim_s"]))):
                    if previous[index] is not None and previous[index] != current:
                        changed[index] = now
                    previous[index] = current
                if any(seen is None or now-seen > .3 for seen in changed):
                    raise ValueError("Waiting for advancing camera and telemetry")
                row.update(status="sample", **pose)
                valid += 1
            except ValueError as exc:
                row.update(status="unknown", reason=str(exc))
            write(row)
            time.sleep(max(0, start+(n+1)/hz-time.perf_counter()))
        return 0 if valid else 2


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=15)
    parser.add_argument("--hz", type=int, default=30)
    parser.add_argument("--out", help="Write JSON lines to a new file (default: stdout)")
    args = parser.parse_args()
    if sys.platform != "win32" or not 1 <= args.seconds <= 300 or not 5 <= args.hz <= 60:
        parser.error("Requires Windows, 1–300 seconds and a sample rate of 5–60 Hz.")
    with ExitStack() as files:
        stream = files.enter_context(open(args.out, "x", encoding="utf-8")) if args.out else sys.stdout
        sys.exit(run(args.seconds, args.hz, stream))
