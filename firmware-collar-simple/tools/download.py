"""download.py - downloads the collar uploads stored on the Render website
(backend-fastapi, /api/v1/collar/uploads) into uploads/, then deletes them there.

Run:   python download.py            download everything, then delete it from the website
       python download.py --keep     download but leave it on the website
       python download.py --list     only show what is stored

Needs two environment variables (do NOT put the key in a file you push to GitHub):
    COWNECT_URL      e.g. https://cownect-backend.onrender.com
    COWNECT_API_KEY  the COLLAR_API_KEY set in the Render dashboard
PowerShell:  $env:COWNECT_URL = "https://..." ; $env:COWNECT_API_KEY = "..."

Files are named like receiver.py names them (local time the website received it):
    uploads/<date>_<time>_dev<id>_cap<id>.bin
so decode.py works on them the same way. A file is deleted from the website only
after it was saved here with the right size.
"""
import json
import os
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

UPLOAD_DIR = Path(__file__).resolve().parent / "uploads"
TIMEOUT_S = 120          # a sleeping free Render server can take ~1 min to wake up


def request(method, path):
    url = os.environ["COWNECT_URL"].rstrip("/") + "/api/v1/collar/uploads" + path
    req = urllib.request.Request(url, method=method, headers={"X-API-Key": os.environ["COWNECT_API_KEY"]})
    with urllib.request.urlopen(req, timeout=TIMEOUT_S) as r:
        return r.read()


def file_name(u):
    received = datetime.fromisoformat(u["received_at"])
    if received.tzinfo is None:                     # no time zone given = UTC
        received = received.replace(tzinfo=timezone.utc)
    received = received.astimezone()                # UTC -> local time
    return f"{received:%Y-%m-%d_%H%M%S}_dev{u['device_id']}_cap{u['capture_id']}.bin"


def main():
    if not os.environ.get("COWNECT_URL") or not os.environ.get("COWNECT_API_KEY"):
        sys.exit("set COWNECT_URL and COWNECT_API_KEY first (see the top of download.py)")
    keep, list_only = "--keep" in sys.argv, "--list" in sys.argv

    try:
        uploads = json.loads(request("GET", ""))
    except urllib.error.HTTPError as e:
        sys.exit(f"website answered HTTP {e.code}" + (" - check COWNECT_API_KEY" if e.code == 401 else ""))
    total = sum(u["size_bytes"] for u in uploads)
    print(f"{len(uploads)} uploads on the website, {total / 1e6:.1f} MB")
    if list_only:
        for u in uploads:
            print(f"   #{u['id']:<5} {file_name(u)}  {u['size_bytes']:>10,} bytes"
                  + ("" if u["complete"] else "  INCOMPLETE"))
        return

    UPLOAD_DIR.mkdir(exist_ok=True)
    for u in uploads:
        path = UPLOAD_DIR / file_name(u)
        data = request("GET", f"/{u['id']}/data")
        if len(data) != u["size_bytes"]:
            print(f"   #{u['id']}: got {len(data):,} of {u['size_bytes']:,} bytes - not saved, kept on the website")
            continue
        path.write_bytes(data)
        state = "" if u["complete"] else "  (INCOMPLETE upload)"
        if not keep:
            request("DELETE", f"/{u['id']}")
        print(f"   saved {path.name}{state}" + ("" if keep else ", deleted from the website"))


if __name__ == "__main__":
    main()
