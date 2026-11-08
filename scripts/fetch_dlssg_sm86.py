"""Stage the dlssg_sm86 Ampere/Turing frame-generation unlock beside a Magpie build.

The unlock is taken from a DLSS-Unlocked standalone release (ShyVortex/dlss-unlocked), which
packages sdli1995's dlssg_sm86 under OptiScaler/dlssg_sm86/. Nothing is committed to this
repository: the files are proprietary or third-party binaries and are fetched per build.

    python scripts/fetch_dlssg_sm86.py --dest publish/x64            # latest release
    python scripts/fetch_dlssg_sm86.py --dest publish/x64 --zip X.zip  # an already downloaded zip

The download is checked against the SHA-256 digest GitHub publishes for the release asset.
"""
import argparse
import hashlib
import json
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

REPO = "ShyVortex/dlss-unlocked"
PREFIX = "OptiScaler/dlssg_sm86/"


def latest_asset() -> dict:
    req = urllib.request.Request(
        f"https://api.github.com/repos/{REPO}/releases/latest",
        headers={"Accept": "application/vnd.github+json", "User-Agent": "magpie-dlssg-sm86"},
    )
    with urllib.request.urlopen(req) as resp:
        release = json.load(resp)
    for asset in release["assets"]:
        if "standalone" in asset["name"] and asset["name"].endswith(".zip"):
            asset["tag"] = release["tag_name"]
            return asset
    sys.exit(f"release {release['tag_name']} has no standalone zip")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dest", required=True, help="folder holding Magpie.exe")
    ap.add_argument("--zip", help="local DLSS-Unlocked standalone zip; skips the download")
    args = ap.parse_args()

    dest = Path(args.dest)
    if not dest.is_dir():
        sys.exit(f"destination not found: {dest}")

    with tempfile.TemporaryDirectory() as tmp:
        if args.zip:
            zpath = Path(args.zip)
        else:
            asset = latest_asset()
            zpath = Path(tmp) / asset["name"]
            print(f"downloading {asset['name']} ({asset['size'] / 1e6:.0f} MB) from {asset['tag']}")
            urllib.request.urlretrieve(asset["browser_download_url"], zpath)
            want = (asset.get("digest") or "").removeprefix("sha256:")
            got = sha256(zpath)
            if not want:
                sys.exit("GitHub published no digest for the asset; refusing an unverified binary")
            if got != want:
                sys.exit(f"sha256 mismatch: got {got}, GitHub says {want}")
            print(f"sha256 ok: {got}")

        target = dest / "dlssg_sm86"
        staging = dest / "dlssg_sm86.partial"
        shutil.rmtree(staging, ignore_errors=True)
        count = 0
        with zipfile.ZipFile(zpath) as z:
            for name in z.namelist():
                if not name.startswith(PREFIX) or name.endswith("/"):
                    continue
                out = staging / name[len(PREFIX):]
                out.parent.mkdir(parents=True, exist_ok=True)
                with z.open(name) as src, open(out, "wb") as dst:
                    shutil.copyfileobj(src, dst)
                count += 1
        if count == 0 or not (staging / "dlssg_sm86.dll").is_file():
            shutil.rmtree(staging, ignore_errors=True)
            sys.exit(f"no {PREFIX}dlssg_sm86.dll in {zpath}")
        # The packaged ini is kept unless the user already tuned one.
        old_ini = target / "dlssg_sm86.ini"
        if old_ini.is_file():
            shutil.copy2(old_ini, staging / "dlssg_sm86.ini")
        shutil.rmtree(target, ignore_errors=True)
        staging.rename(target)
        print(f"staged {count} files in {target}")


if __name__ == "__main__":
    main()
