#!/usr/bin/env python3
"""Upload a release through the restricted SSH receiver (no remote shell)."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile


def main():
    parser = argparse.ArgumentParser()
    for name in ("tag", "directory", "host", "user", "key", "known-hosts"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"v[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}", args.tag):
        raise ValueError("Invalid version tag")
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9.-]*", args.host) or not re.fullmatch(r"[a-z][a-z0-9-]*", args.user):
        raise ValueError("Invalid SSH target")
    spec = importlib.util.spec_from_file_location("receiver", Path(__file__).with_name("receive-release.py"))
    receiver = importlib.util.module_from_spec(spec); spec.loader.exec_module(receiver)
    directory = Path(args.directory).resolve()
    version = args.tag[1:]
    receiver.validate(directory, version)
    files = [f"OpenBoard-cheyuze-{version}-x64.exe", "update.json", "SHA256SUMS.txt"]
    with tempfile.TemporaryFile() as bundle:
        with tarfile.open(fileobj=bundle, mode="w", format=tarfile.USTAR_FORMAT) as archive:
            for name in files:
                archive.add(directory / name, arcname=name, recursive=False)
        bundle.seek(0)
        subprocess.run(["ssh", "-i", str(Path(args.key).resolve()), "-o", "IdentitiesOnly=yes",
                        "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes",
                        "-o", "UserKnownHostsFile=" + str(Path(args.known_hosts).resolve()),
                        "-o", "ConnectTimeout=20", "-o", "ServerAliveInterval=30",
                        "-o", "ServerAliveCountMax=4", f"{args.user}@{args.host}",
                        "publish " + args.tag], stdin=bundle, check=True, timeout=1800)


if __name__ == "__main__":
    main()
