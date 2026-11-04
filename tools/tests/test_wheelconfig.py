"""Production CLI functions, only disposable project-local files; no children/devices."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import sys
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("wheelconfig", Path(__file__).parents[1] / "wheelconfig.py")
wc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wc)

TEMPLATE = ("[/Wheel.Config/c262046d]\r\nProductName=Stock\r\nLayout=Stock\r\n"
            "ForceFeedback=0.500000\r\nMaxRotationAngle=900.000000\r\n"
            "Wheel_Steer=Axis1&1.0&0.0\r\nWheel_Accelerator=\r\nWheel_Brake=\r\n"
            "Wheel_Clutch=\r\nWheel_Handbrake=\r\nWheel_LeftShoulder=Button13\r\n")
TARGET = TEMPLATE.replace("c262046d", "0006346e").replace("ProductName=Stock", "  ProductName = Old   ; owned comment")
SOURCE = ("; untouched café\r\n" + TEMPLATE + "\r\n" + TARGET + "[Other]\r\nunknown=keep").encode()
CLI_SOURCE = SOURCE.replace(b"  ProductName = Old", b"ProductName=Old")


class WriterTests(unittest.TestCase):
    def setUp(self):
        # The bounded operator redirects TMP/TEMP inside its evidence root.
        self.tmp = tempfile.TemporaryDirectory(prefix="wheelconfig-")
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "WheelConfig.ini"
        self.path.write_bytes(SOURCE)

    def args(self, *extra):
        return wc.ap.parse_args(["--ini", str(self.path), "--product", "0006346e", "--name", "New", *extra])

    def run_cli(self, *extra):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return wc.main(["--ini", str(self.path), "--product", "0006346e", "--name", "New", *extra])

    def no_temp(self):
        self.assertEqual([], list(self.path.parent.glob("*.tmp")))

    def test_existing_exact_bytes_and_no_final_newline(self):
        # Only the target Layout changes; the template is never rewritten.
        expected = SOURCE[:SOURCE.index(b"  ProductName")] + SOURCE[SOURCE.index(b"  ProductName"):].replace(b"Old   ; owned comment", b"New   ; owned comment").replace(b"Layout=Stock", b"Layout=Generic", 1)
        self.assertEqual(0, self.run_cli())
        self.assertEqual(expected, self.path.read_bytes())
        self.assertFalse(self.path.read_bytes().endswith(b"\n"))
        self.assertFalse(Path(str(self.path) + ".bak").exists())
        self.no_temp()

    def test_bom_mixed_line_endings_and_unknown_sections(self):
        raw = b"\xef\xbb\xbf" + SOURCE.replace(b"unknown=keep", b"unknown=keep\n; other LF\n")
        edited, clone, _ = wc.plan(raw, self.args())
        self.assertFalse(clone)
        self.assertTrue(edited.startswith(b"\xef\xbb\xbf; untouched caf\xc3\xa9\r\n" + TEMPLATE.encode()))
        self.assertTrue(edited.endswith(b"[Other]\r\nunknown=keep\n; other LF\n"))

    def test_clone_preserves_entire_prefix_and_old_backup(self):
        original = b"\xef\xbb\xbf" + TEMPLATE.encode() + b"\r\n\r\n; retain EOF"
        self.path.write_bytes(original)
        old = Path(str(self.path) + ".bak")
        old.write_bytes(b"previous valuable backup")
        self.assertEqual(0, self.run_cli())
        self.assertTrue(self.path.read_bytes().startswith(original + b"\r\n\r\n"))
        self.assertEqual(b"previous valuable backup", old.read_bytes())
        backups = list(self.path.parent.glob("*.bak.*"))
        self.assertEqual(1, len(backups))
        self.assertEqual(original, backups[0].read_bytes())
        self.no_temp()

    def test_clone_without_prior_backup(self):
        self.path.write_bytes(TEMPLATE.encode())
        self.assertEqual(0, self.run_cli())
        self.assertEqual(TEMPLATE.encode(), Path(str(self.path) + ".bak").read_bytes())

    def test_refusals_leave_original_without_temp(self):
        cases = [SOURCE + b"\r\n[/Wheel.Config/0006346E]\r\nx=y\r\n",
                 SOURCE.replace(b"  ProductName", b"ProductName=duplicate\r\n  ProductName"),
                 SOURCE.replace(b"[/Wheel.Config/c262046d]", b"[/Wheel.Config/c262046d"),
                 SOURCE + b"\r\ngarbage\r\n", SOURCE + b"\xff", SOURCE + b"\0",
                 SOURCE.replace(b"\r\n", b"\n"), SOURCE + b"\rbare",
                 b"x" * (1024 * 1024 + 1), b"\xff\xfe" + TEMPLATE.encode()]
        for raw in cases:
            with self.subTest(raw=raw[:24]):
                self.path.write_bytes(raw)
                self.assertEqual(1, self.run_cli())
                self.assertEqual(raw, self.path.read_bytes())
                self.no_temp()

    def test_ambiguous_requests_and_missing_fields_refuse(self):
        for args in [("--product", "../bad"), ("--name", "bad\n[Injected]"),
                     ("--set", "Wheel_LeftShoulder"),
                     ("--set", "Wheel_LeftShoulder=Button1", "--set", "wheel_leftshoulder=Button2"),
                     ("--set", "Missing=value")]:
            with self.subTest(args=args):
                self.assertEqual(1, self.run_cli(*args))
                self.assertEqual(SOURCE, self.path.read_bytes())

    def test_mapping_options_and_comment_retention(self):
        a = self.args("--polarity", "high", "--throttle", "Axis3", "--brake", "Axis6", "--ffb", "0.25",
                      "--rotation", "1080", "--clear-buttons", "--set", "Wheel_LeftShoulder=Button14")
        edited, _, _ = wc.plan(SOURCE, a)
        target = edited[edited.index(b"[/Wheel.Config/0006346e]"):]
        for field in (b"Wheel_Accelerator=Axis3&-1.0&1.0", b"Wheel_Brake=Axis6&-1.0&1.0",
                      b"ForceFeedback=0.250000", b"MaxRotationAngle=1080.000000", b"Wheel_LeftShoulder=Button14"):
            self.assertIn(field, target)
        self.assertIn(b"New   ; owned comment\r\n", target)

    def test_replace_failure_keeps_old_file_and_cleans_temp(self):
        with mock.patch.object(wc, "replace", side_effect=PermissionError("locked destination")):
            self.assertEqual(1, self.run_cli())
        self.assertEqual(SOURCE, self.path.read_bytes())
        self.no_temp()

    def test_flush_failure_keeps_old_file_and_cleans_temp(self):
        with mock.patch.object(wc.os, "fsync", side_effect=OSError("flush failed")):
            self.assertEqual(1, self.run_cli())
        self.assertEqual(SOURCE, self.path.read_bytes())
        self.no_temp()

    def test_changed_source_refuses_replacement(self):
        info = wc.inspect(self.path)
        edited, clone, _ = wc.plan(SOURCE, self.args())
        self.path.write_bytes(SOURCE + b"\r\n; external change\r\n")
        with self.assertRaisesRegex(ValueError, "source changed"):
            wc.save(self.path, SOURCE, info, edited, clone)
        self.assertTrue(self.path.read_bytes().endswith(b"; external change\r\n"))
        self.no_temp()

    def test_noop_does_not_replace_or_change_timestamp(self):
        edited, _, _ = wc.plan(SOURCE, self.args())
        self.path.write_bytes(edited)
        before = self.path.stat().st_mtime_ns
        with mock.patch.object(wc, "replace", side_effect=AssertionError("unnecessary replace")):
            self.assertEqual(0, self.run_cli())
        self.assertEqual(before, self.path.stat().st_mtime_ns)

    def test_hardlink_refuses_without_touching_target(self):
        alias = self.path.parent / "alias.ini"
        os.link(self.path, alias)
        self.assertEqual(1, self.run_cli())
        self.assertEqual(SOURCE, alias.read_bytes())

    def test_empty_owned_value_retains_comment(self):
        self.assertEqual("x=New ; keep\r\n", wc.setf("x=; keep\r\n", "x", "New"))
        self.assertEqual(" x = New   # keep\n", wc.setf(" x = Old   # keep\n", "X", "New"))

    @unittest.skipUnless(os.name == "nt", "Windows file attributes")
    def test_readonly_refuses_unchanged(self):
        os.chmod(self.path, 0o444)
        try:
            self.assertEqual(1, self.run_cli())
            self.assertEqual(SOURCE, self.path.read_bytes())
            self.no_temp()
        finally:
            os.chmod(self.path, 0o666)

    @unittest.skipUnless(os.name == "nt", "Windows replacement lock")
    def test_real_locked_destination_refuses(self):
        import ctypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        create = kernel.CreateFileW
        create.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong,
                           ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
        create.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = create(str(self.path), 0x80000000, 1, None, 3, 0, None)
        self.assertNotEqual(ctypes.c_void_p(-1).value, handle)
        try:
            self.assertEqual(1, self.run_cli())
            self.assertEqual(SOURCE, self.path.read_bytes())
            self.no_temp()
        finally:
            kernel.CloseHandle(handle)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] in ("--prepare-cli", "--verify-cli"):
        root = Path(sys.argv[2])
        old, fixed = root / "baseline.ini", root / "fixed.ini"
        if sys.argv[1] == "--prepare-cli":
            old.write_bytes(CLI_SOURCE)
            fixed.write_bytes(CLI_SOURCE)
        else:
            start = CLI_SOURCE.index(b"ProductName=Old")
            expected = CLI_SOURCE[:start] + CLI_SOURCE[start:].replace(b"Old   ; owned comment", b"New   ; owned comment").replace(b"Layout=Stock", b"Layout=Generic", 1)
            assert old.read_bytes() != expected, "old writer must expose the regression"
            assert b"; owned comment" not in old.read_bytes(), "old writer must lose the target comment"
            assert fixed.read_bytes() == expected, "fixed actual CLI must retain all other bytes"
        print(json.dumps({"pass": True, "phase": sys.argv[1]}))
    else:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(WriterTests))
        print(json.dumps({"pass": result.wasSuccessful(), "tests": result.testsRun, "skipped": len(result.skipped), "python": sys.version}))
        sys.exit(0 if result.wasSuccessful() else 1)
