"""Convert a2x cs2-dumper .hpp output into the JSON layout expected by
tools/update_offsets.py (offsets.json / interfaces.json / *_dll.json / info.json).

Usage: python tools/a2x_to_json.py <a2x_dir> <out_dir>
"""
import json
import re
import sys
from pathlib import Path

FIELD = re.compile(r"^\s*constexpr std::ptrdiff_t (\w+) = (0x[0-9a-fA-F]+);")
NS_OPEN = re.compile(r"^\s*namespace ([\w]+) \{")
NS_CLOSE = re.compile(r"^\s*\}")
PARENT = re.compile(r"^\s*// Parent: (\w+)")


def convert_schemas(src: Path, out: Path):
    for hpp in sorted(src.glob("*_dll.hpp")):
        module = hpp.stem[: -len("_dll")] + ".dll"
        classes = {}
        parent = ""
        in_enum = 0
        current = None
        for line in hpp.read_text(encoding="utf-8", errors="replace").splitlines():
            if in_enum:
                in_enum += line.count("{") - line.count("}")
                continue
            if "enum class" in line:
                in_enum = line.count("{") - line.count("}")
                if in_enum == 0 and line.rstrip().endswith(";"):
                    pass
                continue
            m = PARENT.match(line)
            if m:
                parent = "" if m.group(1) == "None" else m.group(1)
                continue
            m = NS_OPEN.match(line)
            if m:
                name = m.group(1)
                if name not in ("cs2_dumper", "schemas", module[: -len(".dll")]):
                    current = name
                    classes.setdefault(name, {"parent": parent, "fields": {}})
                parent = ""
                continue
            m = FIELD.match(line)
            if m and current:
                classes[current]["fields"][m.group(1)] = int(m.group(2), 16)
                continue
        data = {module: {"classes": classes}}
        (out / (hpp.stem + ".json")).write_text(json.dumps(data), encoding="utf-8")
        print(f"{hpp.name}: {len(classes)} classes")


def convert_flat(src: Path, out: Path, hpp_name: str, json_name: str):
    text = (src / hpp_name).read_text(encoding="utf-8", errors="replace")
    result = {}
    stack = []
    for line in text.splitlines():
        m = NS_OPEN.match(line)
        if m:
            stack.append(m.group(1))
            continue
        m = FIELD.match(line)
        if m:
            if len(stack) >= 3 and stack[0] == "cs2_dumper":
                module = stack[2][: -len("_dll")] + ".dll"
                result.setdefault(module, {})[m.group(1)] = int(m.group(2), 16)
            continue
    (out / json_name).write_text(json.dumps(result), encoding="utf-8")
    print(f"{json_name}: {sum(len(v) for v in result.values())} entries in {len(result)} modules")


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    convert_schemas(src, out)
    convert_flat(src, out, "offsets.hpp", "offsets.json")
    convert_flat(src, out, "interfaces.hpp", "interfaces.json")
    # Build number is not present in hpp dumps; use the engine value verified
    # at runtime (game.cpp build check) for this install.
    (out / "info.json").write_text(json.dumps({"build_number": 14188}), encoding="utf-8")
    print("info.json: build_number=14188")


if __name__ == "__main__":
    main()
