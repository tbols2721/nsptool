"""Hardware-free package-integrity and command-help checks."""
import ctypes
import hashlib
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class PackageInfo(ctypes.Structure):
    _fields_ = [("extension", ctypes.c_char * 16), ("major", ctypes.c_uint),
                ("minor", ctypes.c_uint), ("build", ctypes.c_uint)]


class PackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        output = Path(cls.directory.name) / "checks.so"
        subprocess.run(["gcc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra",
                        str(ROOT / "ospkg.c"), str(ROOT / "sha256.c"),
                        "-o", str(output)], check=True)
        cls.library = ctypes.CDLL(str(output))
        cls.library.ospkg_verify.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                            ctypes.POINTER(PackageInfo)]
        cls.library.ospkg_verify.restype = ctypes.c_int
        cls.library.sha256.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
        cls.library.sha256.restype = None

    def package(self, payload=b"synthetic firmware", digest=None, include_payload=True,
                extra=False, compression=zipfile.ZIP_STORED):
        digest = digest or hashlib.sha256(payload).hexdigest()
        manifest = f"# TI-Nspire.tcc2\n6.40.74\n6.40.74\nimage.dat\n{digest}\n"
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w", compression=compression) as archive:
            archive.writestr("manifest.txt", manifest)
            if include_payload:
                archive.writestr("image.dat", payload)
            if extra:
                archive.writestr("unlisted.dat", b"unlisted synthetic data")
        return data.getvalue()

    def verify(self, data):
        raw = ctypes.create_string_buffer(data)
        info = PackageInfo()
        return self.library.ospkg_verify(raw, len(data), ctypes.byref(info)), info

    def test_sha256_matches_standard_library_across_block_boundaries(self):
        for data in (b"", b"abc", b"a" * 55, b"a" * 56, b"a" * 64,
                     bytes(range(256)) * 9):
            with self.subTest(size=len(data)):
                raw, digest = ctypes.create_string_buffer(data), (ctypes.c_ubyte * 32)()
                self.library.sha256(raw, len(data), digest)
                self.assertEqual(bytes(digest), hashlib.sha256(data).digest())

    def test_valid_synthetic_package(self):
        result, info = self.verify(self.package())
        self.assertEqual(result, 0)
        self.assertEqual(info.extension, b".tcc2")
        self.assertEqual((info.major, info.minor, info.build), (6, 40, 74))

    def test_changed_payload_is_rejected(self):
        data = self.package(payload=b"changed", digest=hashlib.sha256(b"original").hexdigest())
        self.assertEqual(self.verify(data)[0], 1)

    def test_missing_manifest_payload_is_rejected(self):
        self.assertEqual(self.verify(self.package(include_payload=False))[0], 1)

    def test_unlisted_payload_is_rejected(self):
        self.assertEqual(self.verify(self.package(extra=True))[0], 1)

    def test_compressed_archive_is_rejected(self):
        self.assertEqual(self.verify(self.package(compression=zipfile.ZIP_DEFLATED))[0], 1)

    def test_non_package_is_distinguished(self):
        self.assertEqual(self.verify(b"not an OS package")[0], -1)

    def test_help_commands_do_not_require_usb(self):
        for command in ([str(ROOT / "nsptool"), "--help"],
                        [str(ROOT / "ndless.sh"), "--help"]):
            with self.subTest(command=Path(command[0]).name):
                result = subprocess.run(command, capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("usage:", result.stdout.lower())


if __name__ == "__main__":
    unittest.main()
