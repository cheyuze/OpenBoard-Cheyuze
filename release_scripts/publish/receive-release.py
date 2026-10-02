#!/usr/bin/env python3
"""Restricted SSH receiver: verified, immutable releases; metadata is published last.

Install root-owned at /usr/local/lib/openboard-publish.py. The dedicated SSH
account can write only the OpenBoard static directory, not other applications.
"""
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import signal
import sys
import tarfile
import tempfile
from urllib.parse import urlsplit

VERSION = re.compile(r"[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}")
MAX_PACKAGE = 600 * 1024 * 1024
PUBLIC_ROOT = Path("/opt/platform/proxy/site/openboard")
WORK_ROOT = Path("/var/lib/openboard-publisher/incoming")


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def validate(directory, version):
    if not VERSION.fullmatch(version):
        raise ValueError("Invalid release version")
    name = f"OpenBoard-cheyuze-{version}-x64.exe"
    package = directory / name
    if not 1024 * 1024 <= package.stat().st_size <= MAX_PACKAGE:
        raise ValueError("Installer size outside allowed range")
    with package.open("rb") as stream:
        if stream.read(2) != b"MZ":
            raise ValueError("Not a Windows installer")
    data = (directory / "update.json").read_bytes()
    if len(data) > 256 * 1024:
        raise ValueError("Oversized manifest")
    manifest = json.loads(data)
    expected_site = f"https://xiwang.cheyuze.top/openboard/releases/{version}/{name}"
    expected_github = f"https://github.com/cheyuze/OpenBoard-Cheyuze/releases/download/v{version}/{name}"
    if (manifest.get("version") != version or manifest.get("websiteUrl") != expected_site
            or manifest.get("githubUrl") != expected_github or manifest.get("url") != expected_github):
        raise ValueError("Manifest version or release URLs mismatch")
    if expected_site not in manifest.get("urls", []) or expected_github not in manifest.get("urls", []):
        raise ValueError("Missing official download channels")
    checksum = digest(package)
    if checksum != str(manifest.get("sha256", "")).lower():
        raise ValueError("Installer SHA-256 mismatch")
    if manifest.get("size") != package.stat().st_size:
        raise ValueError("Installer size does not match manifest")
    sums = (directory / "SHA256SUMS.txt").read_text(encoding="utf-8").strip()
    if sums.lower() != f"{checksum}  {name}".lower():
        raise ValueError("SHA256SUMS does not match installer")
    if len(manifest.get("notes", [])) > 30 or not all(isinstance(n, str) for n in manifest.get("notes", [])):
        raise ValueError("Invalid release notes")
    baidu = urlsplit(manifest.get("baiduUrl", ""))
    if baidu.scheme != "https" or baidu.netloc != "pan.baidu.com":
        raise ValueError("Invalid manual download URL")
    return manifest


def unpack(stream, directory, version):
    expected = {f"OpenBoard-cheyuze-{version}-x64.exe": MAX_PACKAGE,
                "update.json": 256 * 1024, "SHA256SUMS.txt": 4096}
    seen = set()
    # Never use extract()/extractall(): paths, links, devices and sparse files
    # from the sender are not allowed to control filesystem operations.
    with tarfile.open(fileobj=stream, mode="r|") as archive:
        for member in archive:
            if (member.name not in expected or member.name in seen or not member.isfile()
                    or member.issparse() or member.pax_headers
                    or not 0 < member.size <= expected[member.name]):
                raise ValueError("Unexpected archive member")
            seen.add(member.name)
            content = archive.extractfile(member)
            destination = directory / member.name
            with destination.open("xb") as output:
                shutil.copyfileobj(content, output, 1024 * 1024)
                output.flush()
                os.fsync(output.fileno())
            if destination.stat().st_size != member.size:
                raise ValueError("Truncated archive member")
            destination.chmod(0o644)
    if seen != set(expected):
        raise ValueError("Incomplete release bundle")
    return validate(directory, version)


def landing_page(manifest):
    version = html.escape(manifest["version"])
    site = html.escape(manifest["websiteUrl"], quote=True)
    baidu = html.escape(manifest["baiduUrl"], quote=True)
    password = html.escape(manifest.get("baiduPassword", ""))
    notes = "".join(f"<li>{html.escape(note)}</li>" for note in manifest.get("notes", []))
    return f'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>OpenBoard {version} · 车厘子定制版</title>
<meta name="description" content="OpenBoard 车厘子定制版 Windows 安装包，网站、GitHub 与百度网盘下载。">
<style>*{{box-sizing:border-box}}body{{margin:0;background:#f3f6fb;color:#20334a;font:16px/1.7 system-ui,'Microsoft YaHei',sans-serif}}
main{{max-width:880px;margin:7vh auto;padding:36px;background:white;border:1px solid #dce5f1;border-radius:22px}}
.tag{{display:inline-block;background:#e8f1ff;color:#2464b0;padding:4px 14px;border-radius:20px;font-weight:600}}
h1{{font-size:32px;line-height:1.3;margin:20px 0 12px}}h2{{font-size:20px}}.lead,.hint{{color:#596e88}}
.downloads{{display:flex;flex-wrap:wrap;gap:12px;margin:28px 0 16px}}.button{{display:inline-block;border:1px solid #c9d9ee;
border-radius:12px;padding:12px 20px;color:#205eaa;text-decoration:none;background:#f5f9ff}}.primary{{background:#286fc1;color:white;border-color:#286fc1}}
.button:hover{{filter:brightness(.95)}}.hint{{font-size:14px}}li{{margin:10px 0}}code{{overflow-wrap:anywhere;font-size:13px}}
footer{{border-top:1px solid #e2e9f3;margin-top:24px;padding-top:16px;font-size:13px;color:#61728a}}@media(max-width:640px){{main{{margin:16px;padding:24px}}h1{{font-size:25px}}}}
</style></head><body><main><span class="tag">Windows x64 · {version}</span><h1>OpenBoard 车厘子定制版</h1>
<p class="lead">为理科教学与讲题录制持续优化的白板。选择适合你网络的下载方式。</p>
<div class="downloads"><a class="button primary" href="{site}" download>网站下载安装包 ↓</a>
<a class="button" href="https://github.com/cheyuze/OpenBoard-Cheyuze/releases/tag/v{version}" rel="noopener">GitHub 下载 ↗</a>
<a class="button" href="{baidu}" rel="noopener">百度网盘 ↗</a></div>
<p class="hint">安装包 {manifest['size'] / 1024 / 1024:.1f} MB · 百度网盘提取码：{password}<br>
百度网盘由维护者手动同步，请核对文件名中的版本号。旧版若自动更新闪退，请从本页下载并手动安装一次。</p>
<h2>本版更新</h2><ul>{notes}</ul><h2>文件校验</h2><code>SHA-256: {manifest['sha256']}</code>
<p class="hint"><a href="releases/{version}/SHA256SUMS.txt">下载校验文件</a> · <a href="update.json">查看更新信息</a></p>
<footer>基于 OpenBoard 的非官方定制版本，保留原版权及 GNU GPL 开源许可。<br>
<a href="https://github.com/cheyuze/OpenBoard-Cheyuze/tree/v{version}">对应版本源代码与许可</a> · <a href="/">返回工具首页</a></footer>
</main></body></html>'''.encode("utf-8")


def atomic_write(path, data):
    # Temp file stays on the same filesystem, then replace is atomic.
    descriptor, name = tempfile.mkstemp(prefix=".publish-", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(data); output.flush(); os.fsync(output.fileno())
        temporary.chmod(0o644)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def publish(staged, manifest, public_root):
    version = manifest["version"]
    current = public_root / "update.json"
    if current.exists():
        old = json.loads(current.read_bytes())
        if tuple(map(int, old["version"].split("."))) > tuple(map(int, version.split("."))):
            raise ValueError("Refusing to roll back the latest version")
    target = public_root / "releases" / version
    if target.exists():
        # Idempotent CI retries are safe, but a version is never overwritten.
        for file in staged.iterdir():
            existing = target / file.name
            if not existing.is_file() or digest(existing) != digest(file):
                raise ValueError("Existing version differs; publish a new version instead")
    else:
        staged.chmod(0o755)
        staged.rename(target)
    atomic_write(public_root / "index.html", landing_page(manifest))
    # All advertised files already exist and passed SHA-256 at this point.
    atomic_write(current, (target / "update.json").read_bytes())
    if os.name == "posix":
        directory = os.open(public_root, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    return target


def interrupted(signum, _frame):
    raise TimeoutError(f"Publication interrupted or timed out (signal {signum})")


def main():
    import fcntl
    # A broken SSH transport must not leave the publication lock held forever.
    # Raising (rather than an abrupt exit) unwinds TemporaryDirectory and flock.
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGALRM, interrupted)
    signal.alarm(25 * 60)
    command = os.environ.get("SSH_ORIGINAL_COMMAND", "")
    match = re.fullmatch(r"publish v([0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4})", command)
    if not match:
        raise ValueError("Only 'publish vMAJOR.MINOR.PATCH' is permitted")
    version = match.group(1)
    with (WORK_ROOT / "publish.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with tempfile.TemporaryDirectory(prefix="release-", dir=WORK_ROOT) as temporary:
            staged = Path(temporary) / "bundle"
            staged.mkdir()
            manifest = unpack(sys.stdin.buffer, staged, version)
            target = publish(staged, manifest, PUBLIC_ROOT)
        print(f"Published {version}; SHA256={manifest['sha256']}; directory={target}")
    signal.alarm(0)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Publication refused: {error}", file=sys.stderr)
        sys.exit(1)
