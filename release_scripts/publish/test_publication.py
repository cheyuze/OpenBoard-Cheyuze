import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("receiver", Path(__file__).with_name("receive-release.py"))
receiver = importlib.util.module_from_spec(spec); spec.loader.exec_module(receiver)


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.public = self.root / "public"
        (self.public / "releases").mkdir(parents=True)

    def tearDown(self):
        self.temp.cleanup()

    def fixture(self, version="1.9.1", payload=None):
        payload = payload or (b"MZ" + b"x" * (1024 * 1024))
        name = f"OpenBoard-cheyuze-{version}-x64.exe"
        checksum = hashlib.sha256(payload).hexdigest()
        site = f"https://xiwang.cheyuze.top/openboard/releases/{version}/{name}"
        github = f"https://github.com/cheyuze/OpenBoard-Cheyuze/releases/download/v{version}/{name}"
        manifest = dict(version=version, url=github, githubUrl=github, websiteUrl=site, urls=[site, github],
                        sha256=checksum, size=len(payload), baiduUrl="https://pan.baidu.com/s/example?pwd=test",
                        baiduPassword="test", notes=["<script>alert(1)</script>"])
        return {name: payload, "update.json": json.dumps(manifest).encode(),
                "SHA256SUMS.txt": f"{checksum}  {name}\n".encode()}

    def bundle(self, files):
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode="w", format=tarfile.USTAR_FORMAT) as tar:
            for name, value in files.items():
                info = tarfile.TarInfo(name); info.size = len(value)
                tar.addfile(info, io.BytesIO(value))
        data.seek(0)
        return data

    def stage(self, files, version="1.9.1"):
        path = Path(tempfile.mkdtemp(dir=self.root))
        manifest = receiver.unpack(self.bundle(files), path, version)
        return path, manifest

    def test_publish_and_retry(self):
        for _ in range(2):
            path, manifest = self.stage(self.fixture())
            receiver.publish(path, manifest, self.public)
        self.assertEqual(json.loads((self.public / "update.json").read_bytes())["version"], "1.9.1")
        page = (self.public / "index.html").read_text(encoding="utf-8")
        self.assertIn("&lt;script&gt;", page)
        self.assertNotIn("<script>", page)

    def test_interrupted_upload_preserves_latest(self):
        path, manifest = self.stage(self.fixture()); receiver.publish(path, manifest, self.public)
        before = (self.public / "update.json").read_bytes()
        files = self.fixture("1.9.2"); files.pop("SHA256SUMS.txt")
        with self.assertRaises(ValueError): self.stage(files, "1.9.2")
        self.assertEqual((self.public / "update.json").read_bytes(), before)

    def test_hash_mismatch(self):
        files = self.fixture(); files["OpenBoard-cheyuze-1.9.1-x64.exe"] += b"corrupt"
        with self.assertRaises(ValueError): self.stage(files)
        self.assertFalse((self.public / "update.json").exists())

    def test_checksum_mismatch(self):
        files = self.fixture(); files["SHA256SUMS.txt"] = b"wrong"
        with self.assertRaises(ValueError): self.stage(files)

    def test_wrong_version(self):
        with self.assertRaises(ValueError): self.stage(self.fixture(), "1.9.2")

    def test_paths_are_never_extracted(self):
        for name in ("../escape", "/etc/escape", "nested/file", "extra.txt"):
            files = self.fixture(); files[name] = b"bad"
            with self.assertRaises(ValueError): self.stage(files)
        self.assertFalse((self.root / "escape").exists())

    def test_symlink_and_oversize_rejected(self):
        for symlink in (True, False):
            data = io.BytesIO()
            with tarfile.open(fileobj=data, mode="w") as tar:
                info = tarfile.TarInfo("update.json")
                if symlink: info.type = tarfile.SYMTYPE; info.linkname = "/etc/passwd"
                else: info.size = 300 * 1024
                tar.addfile(info)
            data.seek(0)
            path = Path(tempfile.mkdtemp(dir=self.root))
            with self.assertRaises(ValueError): receiver.unpack(data, path, "1.9.1")

    def test_existing_version_is_immutable(self):
        path, manifest = self.stage(self.fixture()); receiver.publish(path, manifest, self.public)
        before = (self.public / "update.json").read_bytes()
        path, manifest = self.stage(self.fixture(payload=b"MZ" + b"y" * (1024 * 1024)))
        with self.assertRaises(ValueError): receiver.publish(path, manifest, self.public)
        self.assertEqual((self.public / "update.json").read_bytes(), before)

    def test_downgrade_rejected(self):
        path, manifest = self.stage(self.fixture("1.9.2"), "1.9.2"); receiver.publish(path, manifest, self.public)
        path, manifest = self.stage(self.fixture())
        with self.assertRaises(ValueError): receiver.publish(path, manifest, self.public)
        self.assertEqual(json.loads((self.public / "update.json").read_bytes())["version"], "1.9.2")


if __name__ == "__main__":
    unittest.main(verbosity=2)
