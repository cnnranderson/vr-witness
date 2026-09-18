"""Read-only PE inventory. Never loads game code or writes into its directory."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from datetime import datetime, timezone


class PE:
    """Inspect a PE file on disk without loading or executing its code."""

    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError(f"Not a PE image: {path}")
        self.pe = self.unpack("<I", 0x3C)[0]
        if self.data[self.pe : self.pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        self.machine, count, self.timestamp = self.unpack("<HHI", self.pe + 4)
        optional_size = self.unpack("<H", self.pe + 20)[0]
        optional = self.pe + 24
        self.magic = self.unpack("<H", optional)[0]
        if self.magic not in (0x10B, 0x20B):
            raise ValueError("Unsupported optional header")
        self.is64 = self.magic == 0x20B
        self.image_base = self.unpack("<Q" if self.is64 else "<I", optional + (24 if self.is64 else 28))[0]
        self.image_size = self.unpack("<I", optional + 56)[0]
        self.header_size = self.unpack("<I", optional + 60)[0]
        self.directories = optional + (112 if self.is64 else 96)
        self.sections = []
        for i in range(count):
            start = optional + optional_size + i * 40
            name = self.data[start : start + 8].split(b"\0")[0].decode("ascii", errors="replace")
            vsize, rva, rawsize, offset = self.unpack("<IIII", start + 8)
            self.sections.append(
                dict(name=name, rva=rva, virtual_size=vsize, file_offset=offset, file_size=rawsize)
            )

    def unpack(self, pattern: str, offset: int):
        if offset < 0 or offset + struct.calcsize(pattern) > len(self.data):
            raise ValueError("PE field is out of bounds")
        return struct.unpack_from(pattern, self.data, offset)

    def offset(self, rva: int) -> int:
        """Translate an image-relative address to a file offset, or reject unmapped data."""
        if 0 <= rva < self.header_size:
            return rva
        for section in self.sections:
            delta = rva - section["rva"]
            if 0 <= delta < section["file_size"]:
                return section["file_offset"] + delta
        raise ValueError(f"RVA 0x{rva:x} has no file data")

    def rva(self, offset: int) -> int | None:
        for section in self.sections:
            delta = offset - section["file_offset"]
            if 0 <= delta < section["file_size"]:
                return section["rva"] + delta
        return None

    def cstring(self, rva: int) -> str:
        offset = self.offset(rva)
        end = self.data.find(b"\0", offset, min(offset + 65536, len(self.data)))
        if end == -1:
            raise ValueError("Unterminated PE string")
        return self.data[offset:end].decode("ascii", errors="replace")

    def imports(self):
        result = []
        rva, size = self.unpack("<II", self.directories + 8)
        if not rva:
            return result
        for descriptor in range(min(size // 20, 4096)):
            original, _, _, name, first = self.unpack("<IIIII", self.offset(rva + descriptor * 20))
            if not any((original, name, first)):
                break
            entries = []
            width = 8 if self.is64 else 4
            highbit = 1 << (width * 8 - 1)
            for index in range(65536):
                thunk = self.unpack(
                    "<Q" if self.is64 else "<I", self.offset((original or first) + index * width)
                )[0]
                if not thunk:
                    break
                symbol = f"ordinal:{thunk & 0xffff}" if thunk & highbit else self.cstring(thunk + 2)
                entries.append(dict(name=symbol, iat_rva=f"0x{first + index * width:x}"))
            result.append(dict(dll=self.cstring(name), functions=entries))
        return result

    def markers(self):
        wanted = re.compile(
            r"^(?:IVR\w+_\d+|VR_\w+|ovr_\w+|updateCamera|player_camera_\w+|vr_\w+|teleport_in_vr_enabled|free_camera_speed|stereo_base|OVR:.*)$"
        )
        result = []
        for match in re.finditer(rb"[\x20-\x7e]{4,}\x00", self.data):
            value = match.group()[:-1].decode("ascii")
            if wanted.fullmatch(value):
                rva = self.rva(match.start())
                result.append(
                    dict(
                        text=value,
                        file_offset=f"0x{match.start():x}",
                        rva=None if rva is None else f"0x{rva:x}",
                    )
                )
        return result

    def report(self):
        return dict(
            filename=self.path.name,
            sha256=hashlib.sha256(self.data).hexdigest(),
            size_bytes=len(self.data),
            machine=f"0x{self.machine:04x}",
            image_base=f"0x{self.image_base:x}",
            image_size=self.image_size,
            pe_timestamp=self.timestamp,
            sections=self.sections,
            imports=self.imports(),
            markers=self.markers(),
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--game-dir", type=Path, default=Path(r"D:\SteamLibrary\steamapps\common\The Witness")
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "out/reports/game-inspection.json",
    )
    args = parser.parse_args()
    root = args.game_dir.resolve(strict=True)
    output = args.output.resolve()
    if output == root or root in output.parents:
        parser.error("Report output must be outside the game installation")
    images = [PE(root / name).report() for name in ("witness64_d3d11.exe", "openvr_api.dll")]
    if images[0]["machine"] != "0x8664":
        parser.error("Expected x64 Witness executable")
    report = dict(
        generated_utc=datetime.now(timezone.utc).isoformat(),
        game_directory=str(root),
        images=images,
        limits="Static imports/strings do not establish reachability, active VR support, or camera addresses. No game code was executed.",
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Report: {output}")
    for image in images:
        print(f"{image['filename']}: {image['machine']}, SHA256 {image['sha256']}")
        for module in image["imports"]:
            if any(part in module["dll"].lower() for part in ("d3d", "dxgi", "openvr", "ovr")):
                print(f"  {module['dll']}: " + ", ".join(entry["name"] for entry in module["functions"]))
        print(f"  {len(image['markers'])} VR/camera string markers")


if __name__ == "__main__":
    main()
