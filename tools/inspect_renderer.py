"""Static PE/pattern inspection. Does not load or execute the renderer DLL."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct


class PE:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError("Not a PE file")
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        machine, count, stamp = struct.unpack_from("<HHI", self.data, pe + 4)
        optional_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        optional = pe + 24
        if machine != 0x8664 or struct.unpack_from("<H", self.data, optional)[0] != 0x20B:
            raise ValueError("Expected x64 PE32+")
        self.image_base = struct.unpack_from("<Q", self.data, optional + 24)[0]
        self.image_size = struct.unpack_from("<I", self.data, optional + 56)[0]
        self.timestamp = datetime.fromtimestamp(stamp, timezone.utc).isoformat()
        self.sections = []
        for i in range(count):
            off = optional + optional_size + i * 40
            name = self.data[off:off + 8].rstrip(b"\0").decode("ascii")
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", self.data, off + 8)
            self.sections.append(dict(name=name, rva=rva, virtual_size=virtual_size,
                                      raw_size=raw_size, raw_offset=raw_offset))

    def section(self, name):
        return next(s for s in self.sections if s["name"] == name)

    def bytes(self, section):
        start = section["raw_offset"]
        return self.data[start:start + section["raw_size"]]


def matches(data, pattern):
    tokens = pattern.split()
    expression = b"".join(b"." if t in ("?", "??") else re.escape(bytes([int(t, 16)])) for t in tokens)
    return [m.start() for m in re.finditer(b"(?=(" + expression + b"))", data, re.DOTALL)]


def inspect(path):
    pe = PE(path)
    text = pe.section(".text")
    code = pe.bytes(text)
    source = (Path(__file__).resolve().parents[1] / "cs2_internal/src/game/game.cpp").read_text(encoding="utf-8")
    patterns = re.findall(r'\{\s*"((?:[0-9A-F?]{1,2} )+[0-9A-F?]{1,2})",\s*(\d+),\s*(\d+),\s*(true|false)\s*\}', source)
    result = dict(file=Path(path).name, sha256=hashlib.sha256(pe.data).hexdigest(),
                  pe_timestamp_utc=pe.timestamp, image_base=hex(pe.image_base),
                  image_size=hex(pe.image_size), sections=pe.sections, patterns=[])
    for pattern, displacement_offset, instruction_end, linked_list in patterns:
        found = []
        for off in matches(code, pattern):
            rva = text["rva"] + off
            displacement = struct.unpack_from("<i", code, off + int(displacement_offset))[0]
            target = rva + int(instruction_end) + displacement
            found.append(dict(match_rva=hex(rva), rip_target_rva=hex(target),
                              target_section=next((s["name"] for s in pe.sections
                                  if s["rva"] <= target < s["rva"] + max(s["virtual_size"], s["raw_size"])), None)))
        result["patterns"].append(dict(pattern=pattern, displacement_offset=int(displacement_offset),
                                      instruction_end=int(instruction_end), linked_list=linked_list == "true",
                                      count=len(found), matches=found))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = inspect(args.dll)
    rendered = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
