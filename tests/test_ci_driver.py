"""Regression coverage for Mesa manifest discovery on fresh CI runners."""

from pathlib import Path
import tempfile
import unittest

from scripts.ci import find_lavapipe


class LavapipeDiscoveryTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def manifest(self, name):
        path = self.directory / name
        path.write_text("{}", encoding="utf-8")
        return path.resolve()

    def test_current_mesa_manifest_without_architecture_suffix(self):
        manifest = self.manifest("lvp_icd.json")
        self.assertEqual(find_lavapipe(self.directory, "x86_64"), manifest)

    def test_legacy_mesa_manifest_with_architecture_suffix(self):
        manifest = self.manifest("lvp_icd.x86_64.json")
        self.assertEqual(find_lavapipe(self.directory, "x86_64"), manifest)

    def test_current_manifest_is_preferred_when_both_exist(self):
        self.manifest("lvp_icd.x86_64.json")
        manifest = self.manifest("lvp_icd.json")
        self.assertEqual(find_lavapipe(self.directory, "x86_64"), manifest)

    def test_missing_driver_reports_prerequisite_without_selecting_another_architecture(self):
        self.manifest("lvp_icd.i686.json")
        with self.assertRaisesRegex(RuntimeError, "install mesa-vulkan-drivers"):
            find_lavapipe(self.directory, "x86_64")


if __name__ == "__main__":
    unittest.main()
