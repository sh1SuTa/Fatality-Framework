"""Update only uniquely identified offsets from a pinned cs2-dumper checkout.

No game process is opened. Unmatched and ambiguous entries remain unchanged and
are recorded in offset-update-report.json. This does not port ABI/type layouts.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "cs2_internal/include/sdk"
CONSTANT = re.compile(r"(constexpr\s+(?:auto|std::ptrdiff_t)\s+)(\w+)(\s*=\s*)(0x[0-9a-fA-F]+)(\s*;)")
CLASS_INDEX = {}


def key(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())


def class_key(name):
    # The original generator drops C_ from client class names, but keeps CBase.
    return key(re.sub(r"^C_", "", name))


def field_keys(name):
    result = {key(name)}
    raw = name.lstrip("_")
    if raw.startswith("m_"):
        raw = raw[2:]
    result.add(key(raw))
    # Source2gen strips these Hungarian type tags. Keep other tags such as h,
    # vec, ang and un intact. Only a unique source field can be selected.
    for tag in ("fl", "sz", "n", "i", "b"):
        if raw.startswith(tag) and len(raw) > len(tag) and raw[len(tag)].isupper():
            result.add(key(raw[len(tag):]))
    if raw.startswith("p") and len(raw) > 1 and raw[1].isupper():
        result.add(key(raw[1:] + "_ptr"))
    return result


def constants(text):
    scopes = []
    pending = None
    offset = 0
    for line in text.splitlines(keepends=True):
        code = line.split("//", 1)[0]
        ns = re.search(r"\bnamespace\s+(\w+)", code)
        if ns:
            pending = ns.group(1)
        for char in code:
            if char == "{":
                scopes.append(pending)
                pending = None
            elif char == "}":
                scopes.pop()
        match = CONSTANT.search(code)
        if match:
            yield tuple(s for s in scopes if s), match, offset
        offset += len(line)


def load_classes(output):
    modules = {}
    for path in sorted(output.glob("*_dll.json")):
        for module, data in json.loads(path.read_text(encoding="utf-8")).items():
            if "classes" in data:
                modules[module[: -len(".dll")]] = data["classes"]
    CLASS_INDEX.clear()
    for module, classes in modules.items():
        for name in classes:
            CLASS_INDEX.setdefault((module, class_key(name)), []).append(name)
    return modules


def find_schema(scope, field, modules):
    if scope[:3] != ("sdk", "offsets", "schema"):
        return []
    parts = scope[3:]
    if len(parts) == 2:
        module, old_class = parts
        sources = [(module, modules.get(module, {}))]
    elif len(parts) == 1:
        old_class = parts[0]
        sources = list(modules.items())
    else:
        return []
    wanted = key(field[: -len("_arr")] if field.endswith("_arr") else field)
    found = []
    for module, classes in sources:
        candidates = CLASS_INDEX.get((module, key(old_class)), [])
        for name in candidates:
            # Fields can move to a base class between releases.
            visited = set()
            owner = name
            # These client fields moved from C_CSPlayerPawnBase to its concrete
            # pawn subclass. Do not search arbitrary unrelated classes.
            successor = "C_CSPlayerPawn" if module == "client" and name == "C_CSPlayerPawnBase" else None
            while owner and owner not in visited and owner in classes:
                visited.add(owner)
                data = classes[owner]
                exact = [(f, value) for f, value in data["fields"].items() if f == field]
                matches = exact or [(f, value) for f, value in data["fields"].items()
                                    if wanted in field_keys(f)]
                if matches:
                    found.extend((module, owner, f, value) for f, value in matches)
                    break
                owner = data.get("parent")
                if not owner and successor and successor not in visited:
                    owner = successor
    return found


GLOBAL_MAP = {
    "global_vars": "dwGlobalVars", "ccsgo_input": "dwCSGOInput",
    "game_entity_system": "dwGameEntitySystem",
    "local_player_controller": "dwLocalPlayerController",
    "screen_transform": "dwViewMatrix", "view_render": "dwViewRender",
}


def find_offset(scope, field, offsets, interfaces):
    if scope == ("sdk", "offsets", "globals") and field in GLOBAL_MAP:
        source = GLOBAL_MAP[field]
        return [("client.dll", "globals", source, offsets["client.dll"][source])]
    if scope == ("sdk", "offsets", "cgame_entity_system") and field == "last_entity_index":
        source = "dwGameEntitySystem_highestEntityIndex"
        return [("client.dll", "cgame_entity_system", source, offsets["client.dll"][source])]
    if len(scope) == 4 and scope[:3] == ("sdk", "offsets", "interfaces"):
        module = scope[-1] + ".dll"
        return [(module, "interfaces", name, value)
                for name, value in interfaces.get(module, {}).items() if key(name) == key(field)]
    return []


def update(text, resolver):
    edits, report = [], []
    for scope, match, start in constants(text):
        field, old = match.group(2), int(match.group(4), 16)
        matches = resolver(scope, field)
        # For module-independent schema types, require every matched module to
        # agree on the value, and at most one matching field per module.
        counts = collections.Counter(item[0] for item in matches)
        values = {item[3] for item in matches}
        valid = bool(matches) and len(values) == 1 and max(counts.values()) == 1
        entry = {"symbol": "::".join((*scope, field)), "old": hex(old),
                 "status": "unresolved" if not matches else "ambiguous"}
        if valid:
            value = values.pop()
            entry.update(new=hex(value), status="updated" if old != value else "verified",
                         sources=[{"module": m, "class": c, "field": f} for m, c, f, _ in matches])
            if old != value:
                edits.append((start + match.start(4), start + match.end(4), hex(value)))
        report.append(entry)
    for start, end, value in reversed(edits):
        text = text[:start] + value + text[end:]
    return text, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "reference/cs2-dumper/output")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    modules = load_classes(output)
    offsets = json.loads((output / "offsets.json").read_text())
    interfaces = json.loads((output / "interfaces.json").read_text())
    info = json.loads((output / "info.json").read_text())
    if not isinstance(info.get("build_number"), int) or info["build_number"] <= 0:
        raise ValueError("Invalid build_number in source data")
    if not modules:
        raise ValueError("No schema classes in source data")
    report = {"source": "https://github.com/a2x/cs2-dumper", "info": info, "files": {}}
    repo = output.parent
    if (repo / ".git").exists():
        report["source_commit"] = subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    report["source_sha256"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in sorted(output.glob("*.json"))}
    for name, resolver in (
        ("offsets_schema.h", lambda s, f: find_schema(s, f, modules)),
        ("offsets.h", lambda s, f: find_offset(s, f, offsets, interfaces)),
    ):
        path = SDK / name
        backup = ROOT / "original-offsets" / name
        original = (backup if args.apply and backup.exists() else path).read_bytes().decode("utf-8")
        changed, entries = update(original, resolver)
        changed = re.sub(r"[ \t]+(?=\r?$)", "", changed, flags=re.MULTILINE)
        if args.apply:
            backup.parent.mkdir(exist_ok=True)
            if not backup.exists():
                backup.write_bytes(path.read_bytes())
            path.write_bytes(changed.encode("utf-8"))
        report["files"][name] = entries
        print(name, dict(collections.Counter(e["status"] for e in entries)))
    if args.apply:
        (SDK / "offsets_build.h").write_text(
            "#pragma once\n#include <cstddef>\n#include <cstdint>\n"
            "namespace sdk::offsets::snapshot {\n"
            f"inline constexpr std::uint32_t build_number = {info['build_number']};\n"
            f"inline constexpr std::ptrdiff_t engine_build_number = {hex(offsets['engine2.dll']['dwBuildNumber'])};\n"
            "}\n", encoding="utf-8")
        (ROOT / "offset-update-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
