"""Checks core dependency migration and locked, recoverable reference preparation."""
from pathlib import Path
import shutil
import unittest
from unittest.mock import patch
import uuid

from prepare_reference import prepare


class PreparationTests(unittest.TestCase):
    """Exercises generated files without compiling or changing canonical Rust sources."""

    def setUp(self):
        """Creates isolated project-local manifest fixtures."""
        self.root = Path(__file__).resolve().parent / (".test-work-" + uuid.uuid4().hex)
        self.source = self.root / "source"
        self.build = self.root / "build"
        for package in ("cel-parser", "cel-runtime"):
            directory = self.source / package
            directory.mkdir(parents=True)
            (directory / "Cargo.toml").write_text("[package]\n", encoding="utf-8")
        (self.source / "Cargo.lock").write_text("# canonical pins\n", encoding="utf-8")
        self.addCleanup(shutil.rmtree, self.root)

    def test_dependency_migration_then_locked_repeat(self):
        """Drops the optional dependency, seeds canonical pins, then uses the resolved lock."""
        self.build.mkdir()
        (self.build / "Cargo.toml").write_text('cel-std = "obsolete"\n', encoding="utf-8")
        (self.build / "Cargo.lock").write_text("# obsolete pins\n", encoding="utf-8")
        commands = []

        def build(command, *, check):
            """Emulates only Cargo resolution after observing the generated input files."""
            commands.append(command)
            self.assertTrue(check)
            if "--locked" not in command:
                self.assertEqual((self.build / "Cargo.lock").read_text(), "# canonical pins\n")
                (self.build / "Cargo.lock").write_text("# resolved pins\n", encoding="utf-8")

        with patch("prepare_reference.shutil.which", return_value="cargo"), \
             patch("prepare_reference.subprocess.run", side_effect=build):
            prepare(self.source, self.build)
            prepare(self.source, self.build)
        manifest = (self.build / "Cargo.toml").read_text()
        self.assertNotIn("cel-std", manifest)
        self.assertIn(str(self.source.resolve() / "cel-parser"), manifest)
        self.assertNotIn("--locked", commands[0])
        self.assertIn("--locked", commands[1])
        self.assertEqual((self.build / "Cargo.lock").read_text(), "# resolved pins\n")

    def test_failed_resolution_is_recoverable(self):
        """Retries an unfinished manifest transition without treating its old lock as resolved."""
        from subprocess import CalledProcessError
        with patch("prepare_reference.shutil.which", return_value="cargo"), \
             patch("prepare_reference.subprocess.run",
                   side_effect=CalledProcessError(1, "cargo")):
            with self.assertRaises(CalledProcessError):
                prepare(self.source, self.build)
        with patch("prepare_reference.shutil.which", return_value="cargo"), \
             patch("prepare_reference.subprocess.run") as build:
            prepare(self.source, self.build)
            self.assertNotIn("--locked", build.call_args.args[0])

    def test_changed_canonical_checkout_reseeds_its_own_pins(self):
        """Reinitializes dependency resolution when the selected source checkout changes."""
        other = self.root / "other-source"
        shutil.copytree(self.source, other)
        (other / "Cargo.lock").write_text("# other canonical pins\n", encoding="utf-8")
        with patch("prepare_reference.shutil.which", return_value="cargo"), \
             patch("prepare_reference.subprocess.run"):
            prepare(self.source, self.build)
        observed = []

        def build(command, *, check):
            """Observes the source-specific lock consumed at the Cargo boundary."""
            observed.append((command, (self.build / "Cargo.lock").read_text()))

        with patch("prepare_reference.shutil.which", return_value="cargo"), \
             patch("prepare_reference.subprocess.run", side_effect=build):
            prepare(other, self.build)
        self.assertEqual(observed[0][1], "# other canonical pins\n")
        self.assertNotIn("--locked", observed[0][0])


if __name__ == "__main__":
    unittest.main()
