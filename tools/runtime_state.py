"""Read a few verified-build globals; never inject, initialize VR, or write game memory."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import struct
import time

from inspect_game import PE

EXPECTED_SHA256 = "8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5"
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
PROCESS_VM_READ = 0x0010
SNAP_MODULE = 0x08
INVALID_HANDLE = ctypes.c_void_p(-1).value


class ModuleEntry(ctypes.Structure):
    _fields_ = [
        ("size", w.DWORD), ("module_id", w.DWORD), ("pid", w.DWORD),
        ("global_count", w.DWORD), ("process_count", w.DWORD),
        ("base", ctypes.c_void_p), ("image_size", w.DWORD), ("module", w.HMODULE),
        ("name", w.WCHAR * 256), ("path", w.WCHAR * 260),
    ]


class ProcessReader:
    """Own a read-only process handle; callers must close it after use."""
    def __init__(self, pid: int):
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.api.OpenProcess.restype = w.HANDLE
        self.api.CloseHandle.argtypes = [w.HANDLE]
        self.api.CloseHandle.restype = w.BOOL
        self.api.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
        self.api.QueryFullProcessImageNameW.restype = w.BOOL
        self.api.IsWow64Process2.argtypes = [w.HANDLE, ctypes.POINTER(w.WORD), ctypes.POINTER(w.WORD)]
        self.api.IsWow64Process2.restype = w.BOOL
        self.api.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
        self.api.CreateToolhelp32Snapshot.restype = w.HANDLE
        self.api.Module32FirstW.argtypes = [w.HANDLE, ctypes.POINTER(ModuleEntry)]
        self.api.Module32FirstW.restype = w.BOOL
        self.api.Module32NextW.argtypes = [w.HANDLE, ctypes.POINTER(ModuleEntry)]
        self.api.Module32NextW.restype = w.BOOL
        self.api.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        self.api.ReadProcessMemory.restype = w.BOOL
        self.handle = self.api.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        self.pid = pid

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None

    def executable(self) -> Path:
        buffer = ctypes.create_unicode_buffer(32768)
        size = w.DWORD(len(buffer))
        if not self.api.QueryFullProcessImageNameW(self.handle, 0, buffer, ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        process_machine, native_machine = w.WORD(), w.WORD()
        if not self.api.IsWow64Process2(self.handle, ctypes.byref(process_machine), ctypes.byref(native_machine)):
            raise ctypes.WinError(ctypes.get_last_error())
        if (process_machine.value or native_machine.value) != 0x8664:
            raise ValueError("The target must be x64")
        return Path(buffer.value).resolve(strict=True)

    def modules(self):
        snapshot = self.api.CreateToolhelp32Snapshot(SNAP_MODULE, self.pid)
        if snapshot == INVALID_HANDLE:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            entry = ModuleEntry()
            entry.size = ctypes.sizeof(entry)
            if not self.api.Module32FirstW(snapshot, ctypes.byref(entry)):
                raise ctypes.WinError(ctypes.get_last_error())
            result = []
            while True:
                result.append(dict(name=entry.name, path=entry.path, base=entry.base, image_size=entry.image_size))
                if not self.api.Module32NextW(snapshot, ctypes.byref(entry)):
                    if ctypes.get_last_error() != 18:  # ERROR_NO_MORE_FILES
                        raise ctypes.WinError(ctypes.get_last_error())
                    return result
        finally:
            self.api.CloseHandle(snapshot)

    def read(self, address: int, size: int) -> bytes:
        """Read exactly size bytes or raise OSError/ValueError; never return a partial read."""
        buffer = ctypes.create_string_buffer(size)
        received = ctypes.c_size_t()
        if not self.api.ReadProcessMemory(self.handle, address, buffer, size, ctypes.byref(received)):
            raise ctypes.WinError(ctypes.get_last_error())
        if received.value != size:
            raise ValueError(f"Incomplete memory read at 0x{address:x}")
        return buffer.raw


def snapshot(reader: ProcessReader, expected: Path) -> dict:
    """Verify executable identity and live references, then return an unsynchronized VR snapshot."""
    executable = reader.executable()
    if executable.name.lower() != "witness64_d3d11.exe" or not executable.samefile(expected):
        raise ValueError("Target does not match the expected Witness executable")
    pe = PE(executable)
    digest = hashlib.sha256(pe.data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError("Unsupported executable hash; no build-specific memory was read")
    modules = reader.modules()
    matches = [m for m in modules if Path(m["path"]).resolve() == executable]
    if len(matches) != 1:
        raise ValueError("Cannot uniquely locate the game's main image")
    main = matches[0]
    if main["image_size"] != pe.image_size:
        raise ValueError("Loaded image size differs from the inspected executable")
    base = main["base"]

    # Verify live RIP-relative references as well as the disk hash.
    verified = []
    for rva, size in ((0x37A436, 7), (0x37A4F9, 7), (0x37A3F6, 6), (0x06722F, 7), (0x2A4041, 66)):
        offset = pe.offset(rva)
        if reader.read(base + rva, size) != pe.data[offset:offset + size]:
            raise ValueError(f"Live instruction mismatch at RVA 0x{rva:x}")
        verified.append(hex(rva))

    # One contiguous read narrows the race window but does not make the snapshot atomic.
    block = reader.read(base + 0x469AB30, 0x80)
    values = {}
    for name, rva, pattern in (
        ("openvr_system_pointer", 0x469AB38, "<Q"),
        ("openvr_compositor_pointer", 0x469AB40, "<Q"),
        ("openvr_init_token_cache", 0x469AB78, "<I"),
    ):
        value = struct.unpack_from(pattern, block, rva - 0x469AB30)[0]
        values[name] = dict(rva=hex(rva), value=hex(value) if pattern == "<Q" else value)

    # Registered camera settings; their effect on the rendered camera is unverified.
    settings_pointer = struct.unpack("<Q", reader.read(base + 0x61C1A0, 8))[0]
    settings = dict(pointer=hex(settings_pointer), semantics="Registered scalar settings, not a verified camera pose")
    if settings_pointer:
        raw = reader.read(settings_pointer + 0xC0, 12)
        settings["registered_camera_scalars"] = dict(zip(
            ("player_camera_forward", "player_camera_left", "player_camera_up"), struct.unpack("<fff", raw)))

    runtime_names = {"d3d11.dll", "dxgi.dll", "openvr_api.dll", "vrclient_x64.dll", "libovrrt64_1.dll", "openxr_loader.dll"}
    return dict(
        generated_utc=datetime.now(timezone.utc).isoformat(), pid=reader.pid,
        executable=str(executable), sha256=digest, image_base=hex(base),
        mode="read_only_external_snapshot", verified_instruction_rvas=verified,
        values=values, settings=settings,
        loaded_graphics_vr_modules=[m["name"] for m in modules if m["name"].lower() in runtime_names],
        limits="No functions called in the game. A null pointer does not prove initialization was never attempted. Snapshot is not synchronized with the game thread.",
    )


def wait_for_vr(reader: ProcessReader, expected: Path, timeout: int) -> dict:
    """Wait up to timeout seconds for both VR interfaces; raise ValueError on timeout."""
    # Verify before polling the two interface slots, then revalidate before returning.
    report = snapshot(reader, expected)
    base = int(report["image_base"], 16)
    slots = [int(report["values"][name]["rva"], 16) for name in
             ("openvr_system_pointer", "openvr_compositor_pointer")]
    deadline = time.monotonic() + timeout
    next_notice = time.monotonic() + 15
    while True:
        ready = all(struct.unpack("<Q", reader.read(base + rva, 8))[0] for rva in slots)
        if ready:
            final = snapshot(reader, expected)
            if all(int(final["values"][name]["value"], 16) for name in
                   ("openvr_system_pointer", "openvr_compositor_pointer")):
                return final
        if time.monotonic() >= deadline:
            raise ValueError("Timed out waiting for native VR. Check SteamVR/headset connection; "
                             "an already-running non-VR game must be closed and relaunched with -vr.")
        if time.monotonic() >= next_notice:
            print("Still waiting for native VR; wake the headset and check SteamVR.", flush=True)
            next_notice = time.monotonic() + 15
        time.sleep(min(1.0, max(0.0, deadline - time.monotonic())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--game-dir", type=Path, default=Path(r"D:\SteamLibrary\steamapps\common\The Witness"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--wait-seconds", type=int, default=0, help="Wait up to 600 seconds for native VR before writing one report")
    args = parser.parse_args()
    if os.name != "nt" or struct.calcsize("P") != 8:
        parser.error("Use 64-bit Python on Windows")
    if not 0 < args.pid <= 0xFFFFFFFF:
        parser.error("PID must be a positive 32-bit integer")
    if not 0 <= args.wait_seconds <= 600:
        parser.error("Wait must be between 0 and 600 seconds")
    game_dir = args.game_dir.resolve(strict=True)
    output = args.output or (Path(__file__).resolve().parents[1] / "out/reports" /
        f"runtime-state-{datetime.now().strftime('%Y%m%d-%H%M%S-%f')}-{args.pid}.json")
    output = output.resolve()
    if output == game_dir or game_dir in output.parents:
        parser.error("Output must be outside the game installation")
    reader = ProcessReader(args.pid)
    try:
        expected = game_dir / "witness64_d3d11.exe"
        report = wait_for_vr(reader, expected, args.wait_seconds) if args.wait_seconds else snapshot(reader, expected)
    finally:
        reader.close()
    output.parent.mkdir(parents=True, exist_ok=True)
    serialized = json.dumps(report, indent=2, allow_nan=False) + "\n"
    with output.open("x", encoding="utf-8") as stream:
        stream.write(serialized)
    print(f"Read-only report: {output}")
    for name, value in report["values"].items():
        print(f"  {name}: {value['value']}")
    print("No game memory was written; no VR functions were called.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(f"Error: {error}") from error
