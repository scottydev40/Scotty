#!/usr/bin/env python3

import tempfile
import unittest
from pathlib import Path

import scotty_hil


class ExperimentalConfigTest(unittest.TestCase):
    def test_replaces_general_setting_and_preserves_other_sections(self) -> None:
        original = "[General]\nName = laptop\nExperimental = true\n\n[Policy]\nAutoEnable=true\n"
        expected = "[General]\nName = laptop\nExperimental = false\n\n[Policy]\nAutoEnable=true\n"
        self.assertEqual(scotty_hil.set_experimental(original, False), expected)

    def test_inserts_into_existing_general_section(self) -> None:
        original = "[General]\nName=laptop\n[Policy]\nAutoEnable=true\n"
        expected = "[General]\nName=laptop\nExperimental = true\n[Policy]\nAutoEnable=true\n"
        self.assertEqual(scotty_hil.set_experimental(original, True), expected)

    def test_adds_general_section_when_missing(self) -> None:
        self.assertEqual(
            scotty_hil.set_experimental("[Policy]\nAutoEnable=true\n", False),
            "[Policy]\nAutoEnable=true\n[General]\nExperimental = false\n",
        )

    def test_replaces_all_active_duplicates(self) -> None:
        original = "[General]\nExperimental=true\nExperimental = false\n"
        expected = "[General]\nExperimental = true\nExperimental = true\n"
        self.assertEqual(scotty_hil.set_experimental(original, True), expected)


class AndroidBondParserTest(unittest.TestCase):
    def test_parses_bonded_list_without_collecting_unbonded_cache(self) -> None:
        dump = """
Bluetooth Status
  mBondedDevices:
    Device{mAddress=AA:BB:CC:DD:EE:FF, mName=laptop}
  AdapterProperties:
    Device{mAddress=11:22:33:44:55:66, bondState=10}
"""
        self.assertEqual(
            scotty_hil.parse_bonded_macs(dump), {"AA:BB:CC:DD:EE:FF"}
        )
        self.assertTrue(scotty_hil.has_bond_dump_schema(dump))

    def test_parses_adjacent_bond_state(self) -> None:
        dump = """
    address: 12:34:56:78:9A:BC
    name: laptop
    bondState=12
"""
        self.assertEqual(
            scotty_hil.parse_bonded_macs(dump), {"12:34:56:78:9A:BC"}
        )

    def test_rejects_dump_without_bond_schema(self) -> None:
        self.assertFalse(scotty_hil.has_bond_dump_schema("Bluetooth service unavailable\n"))


class HelpersTest(unittest.TestCase):
    def test_busctl_scalars(self) -> None:
        self.assertEqual(scotty_hil.parse_busctl_scalar("i 4\n"), "4")
        self.assertEqual(scotty_hil.parse_busctl_scalar('s "laptop"\n'), "laptop")
        self.assertEqual(scotty_hil.parse_busctl_scalar("b false\n"), "false")

    def test_systemd_join_does_not_use_shell_quotes(self) -> None:
        self.assertEqual(
            scotty_hil.systemd_join(["/usr/lib/bluetooth/bluetoothd", "--noplugin=mcp"]),
            "/usr/lib/bluetooth/bluetoothd --noplugin=mcp",
        )
        self.assertEqual(
            scotty_hil.systemd_join(["/opt/blue tooth/bluetoothd"]),
            '"/opt/blue tooth/bluetoothd"',
        )

    def test_alias_classifier(self) -> None:
        self.assertTrue(scotty_hil.looks_rotating_alias("AbCdEf0123456789=="))
        self.assertFalse(scotty_hil.looks_rotating_alias("laptop"))
        self.assertFalse(scotty_hil.looks_rotating_alias("My Laptop Name"))

    def test_hash(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "payload"
            path.write_bytes(b"abc")
            self.assertEqual(
                scotty_hil.sha256_path(path),
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            )


if __name__ == "__main__":
    unittest.main()
