"""Offline verification of the offset patch; no game or downloaded code runs."""
import hashlib
import json
import re
import sys
import unittest

import update_offsets as updater


class MappingTests(unittest.TestCase):
    def test_signed_and_raw_names_are_not_confused(self):
        self.assertIn("health", updater.field_keys("m_iHealth"))
        self.assertIn("mihealth", updater.field_keys("m_iHealth"))
        self.assertNotIn("playerpawn", updater.field_keys("m_hPlayerPawn"))

    def test_pointer_alias(self):
        self.assertIn("ingamemoneyservicesptr", updater.field_keys("m_pInGameMoneyServices"))

    def test_ambiguous_source_does_not_overwrite(self):
        text = "namespace sdk {\nconstexpr auto value = 0x10;\n}\n"
        changed, entries = updater.update(text, lambda *_: [("client", "A", "a", 32), ("client", "A", "b", 32)])
        self.assertEqual(changed, text)
        self.assertEqual(entries[0]["status"], "ambiguous")

    def test_module_disagreement_does_not_overwrite(self):
        text = "namespace sdk {\nconstexpr auto value = 0x10;\n}\n"
        changed, entries = updater.update(text, lambda *_: [("client", "A", "a", 32), ("server", "A", "a", 40)])
        self.assertEqual(changed, text)
        self.assertEqual(entries[0]["status"], "ambiguous")

    def test_missing_source_retains_original(self):
        text = "namespace sdk {\nconstexpr std::ptrdiff_t value = 0x10; // type\n}\n"
        changed, entries = updater.update(text, lambda *_: [])
        self.assertEqual(changed, text)
        self.assertEqual(entries[0]["status"], "unresolved")

    def test_matching_modules_can_share_a_field(self):
        text = "namespace sdk {\nconstexpr auto value = 0x10;\n}\n"
        changed, entries = updater.update(text, lambda *_: [("client", "A", "a", 32), ("server", "A", "a", 32)])
        self.assertIn("value = 0x20", changed)
        self.assertEqual(entries[0]["status"], "updated")

    def test_repeat_update_is_idempotent(self):
        text = "namespace sdk {\nconstexpr auto value = 0x10;\n}\n"
        resolver = lambda *_: [("client", "A", "a", 32)]
        changed, _ = updater.update(text, resolver)
        again, entries = updater.update(changed, resolver)
        self.assertEqual(again, changed)
        self.assertEqual(entries[0]["status"], "verified")


def verify_patch():
    root = updater.ROOT
    output = root / "reference/cs2-dumper/output"
    report = json.loads((root / "offset-update-report.json").read_text())
    for filename, digest in report["source_sha256"].items():
        assert hashlib.sha256((output / filename).read_bytes()).hexdigest() == digest, filename
    datasets = {}
    for file in output.glob("*.json"):
        data = json.loads(file.read_text())
        for module in data:
            if module.endswith(".dll") and isinstance(data[module], dict) and "classes" in data[module]:
                datasets[module.removesuffix(".dll")] = data[module]["classes"]
    global_data = json.loads((output / "offsets.json").read_text())
    interfaces = json.loads((output / "interfaces.json").read_text())
    checked = 0
    for filename, entries in report["files"].items():
        original = (root / "original-offsets" / filename).read_bytes().decode()
        patched = (updater.SDK / filename).read_bytes().decode()
        # Changing numbers must not delete, add or rename any SDK declarations.
        normalize = lambda text: re.sub(r"[ \t]+(?=\r?$)", "", re.sub(r"0x[0-9a-fA-F]+", "HEX", text), flags=re.MULTILINE)
        assert normalize(original) == normalize(patched), filename
        actual = {"::".join((*scope, match.group(2))): int(match.group(4), 16)
                  for scope, match, _ in updater.constants(patched)}
        assert len(actual) == len(entries), filename
        for entry in entries:
            expected = int(entry.get("new", entry["old"]), 16)
            assert actual[entry["symbol"]] == expected, entry["symbol"]
            for source in entry.get("sources", []):
                module, owner, field = source["module"], source["class"], source["field"]
                if owner == "interfaces":
                    value = interfaces[module][field]
                elif module.endswith(".dll"):
                    value = global_data[module][field]
                else:
                    value = datasets[module][owner]["fields"][field]
                assert expected == value, entry["symbol"]
            checked += 1
    # Critical fields have moved in this build. Compare directly to raw names.
    client = datasets["client"]
    assert actual  # Both header inventories were checked above.
    schema = {e["symbol"]: e for e in report["files"]["offsets_schema.h"]}
    for legacy, owner, field in (
        ("base_entity::m_iHealth", "C_BaseEntity", "m_iHealth"),
        ("ccsplayer_controller::h_player_pawn", "CCSPlayerController", "m_hPlayerPawn"),
        ("csplayer_pawn_base::m_bIsScoped", "C_CSPlayerPawn", "m_bIsScoped"),
    ):
        entry = schema["sdk::offsets::schema::client::" + legacy]
        assert int(entry["new"], 16) == client[owner]["fields"][field]
    print(f"PASS: {checked} constants audited; source hashes and critical fields match build {report['info']['build_number']}.")


if __name__ == "__main__":
    result = unittest.main(exit=False).result
    if not result.wasSuccessful():
        sys.exit(1)
    verify_patch()
