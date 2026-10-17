#!/usr/bin/env python3
"""
fetch_mixamo.py - best-effort downloader for the robot-showcase animation clips.

IMPORTANT / honesty up front
----------------------------
Mixamo has NO official/public download API. This script drives the SAME private
JSON API the mixamo.com web app uses, authenticated with YOUR OWN logged-in
session token. That means:
  * It can break any time Adobe changes the endpoints.
  * Use it only for your own account + free assets (respect Mixamo's terms).
  * You must paste in two values from your browser (token + character id).

It downloads each clip "Without Skin" (skeleton + animation only, smaller) at
30fps and saves it next to this file with the exact filename the engine's robot
controller expects (the clip name == the filename). You still need robot.fbx
(the rigged mesh "With Skin") — download that one normally, see README.txt.

Setup
-----
  pip install requests

  1) Sign in at https://www.mixamo.com and select the SAME character you used
     for robot.fbx (so the bones match and the clips retarget cleanly).
  2) Open the browser DevTools (F12) -> Network tab. Click any animation so the
     site makes a request to www.mixamo.com/api/v1/... . Click that request:
       - Request Headers -> copy the value after "authorization: Bearer "  -> TOKEN
     Then manually download ONE animation; in the Network tab find the POST to
     .../animations/export and copy "character_id" from its JSON body -> CHARACTER_ID
  3) Run:
       MIXAMO_TOKEN="eyJ..." MIXAMO_CHARACTER_ID="xxxxxxxx-...." python3 fetch_mixamo.py
     (or pass --token / --character-id, or edit the constants below)

  Add --pistol to fetch the pistol set the Robot Arena uses (pistol_idle / _walk /
  _run / _shoot), --turns for turn_left / turn_right (turning in place), or --all
  for every optional clip (turns, crouch, rifle, pistol).
  Mixamo's names change; each clip tries a few search terms and prints the
  animation it used. Check names yourself with:  --search "pistol"
"""

import argparse
import os
import sys
import time

try:
    import requests
except ImportError:
    sys.exit("This script needs 'requests':  pip install requests")

API = "https://www.mixamo.com/api/v1"
OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# output filename (== engine clip name)  ->  (Mixamo search query or list of
# queries tried in order, force in-place)
CORE_CLIPS = {
    "idle": ("Breathing Idle", True),
    "walk": ("Walking", True),
    "run":  ("Running", True),
    "jump": ("Jump", False),
}
EXTRA_CLIPS = {
    "crouch_idle":  ("Crouching Idle", True),
    "crouch_walk":  ("Crouched Walking", True),
    "rifle_idle":   ("Rifle Aiming Idle", True),
    "rifle_walk":   ("Walk With Rifle", True),
    "rifle_run":    ("Jog Forward (Rifle)", True),
    "rifle_jump":   ("Rifle Jump", False),
    "rifle_crouch": ("Rifle Crouching Idle", True),
}
# Turning in place (~90 degrees each). The Robot Arena plays them when A/D is pressed
# while standing, and rotates the robot itself only when a clip ends, so the
# clip's own hip rotation isn't doubled.
TURN_CLIPS = {
    "turn_left":  (["Left Turn", "Left Turn 90", "Standing Turn Left 90"], True),
    "turn_right": (["Right Turn", "Right Turn 90", "Standing Turn Right 90"], True),
}
# Used by the Robot Arena showcase when present (holding the blaster).
PISTOL_CLIPS = {
    "pistol_idle":  (["Pistol Idle", "Pistol Aiming Idle"], True),
    "pistol_walk":  (["Pistol Walk", "Walking With Pistol"], True),
    "pistol_run":   (["Pistol Run", "Run With Pistol"], True),
    "pistol_shoot": (["Shooting Pistol", "Pistol Shoot", "Pistol Shooting"], True),
}


def headers(token):
    return {
        "Authorization": "Bearer " + token,
        "X-Api-Key": "mixamo2",            # Mixamo's public web key
        "Accept": "application/json",
        "Content-Type": "application/json",
        "Origin": "https://www.mixamo.com",
        "Referer": "https://www.mixamo.com/",
    }


def request(session, method, url, **kwargs):
    """HTTP request that waits and retries when Mixamo rate-limits (HTTP 429)."""
    for attempt in range(8):
        r = session.request(method, url, **kwargs)
        if r.status_code != 429:
            return r
        wait = int(r.headers.get("Retry-After", 0)) or min(60, 10 * (attempt + 1))
        print(f"  (rate limited by Mixamo, waiting {wait}s)")
        time.sleep(wait)
    return r


def search_products(session, query, limit=24):
    r = request(session, "GET", API + "/products", params={
        "page": 1, "limit": limit, "order": "", "type": "Motion,MotionPack", "query": query,
    })
    r.raise_for_status()
    return r.json().get("results", [])


def _words(text):
    return text.lower().replace("(", " ").replace(")", " ").replace("-", " ").split()


def find_product(session, query):
    """Best Motion product for `query` (a string, or a list of strings tried in
    order): an exact name match, else the name containing every query word with
    the fewest extra words (so "Pistol Walk" doesn't pick "Pistol Walk Backward"
    when a plain one exists). Returns (product, names seen) or (None, names seen)."""
    seen = []
    for q in ([query] if isinstance(query, str) else query):
        results = search_products(session, q)
        seen += [p["name"] for p in results[:8]]
        want = _words(q)
        best = None
        for i, p in enumerate(results):
            name = p["name"]
            if name.lower() == q.lower():
                return p, seen
            words = _words(name)
            if all(w in words for w in want):
                key = (len(words) - len(want), i)
                if best is None or key < best[0]:
                    best = (key, p)
        if best:
            return best[1], seen
    return None, seen


def wait_for_job(session, character_id, job_uuid, label):
    """Poll the per-character monitor until this export job finishes; returns the
    download URL or None."""
    for _ in range(90):
        time.sleep(3)
        m = request(session, "GET", API + f"/characters/{character_id}/monitor")
        if m.status_code != 200:
            continue
        js = m.json()
        if job_uuid and js.get("job_uuid") not in (None, job_uuid):
            continue  # an older job (e.g. from a previous run) is reporting
        status = js.get("status")
        if status == "completed":
            return js.get("job_result")
        if status == "failed":
            print(f"  [fail] Mixamo reported the export of '{label}' failed")
            return None
    print(f"  [fail] timed out waiting for '{label}'")
    return None


def export_and_download(session, character_id, query, out_name, in_place):
    product, seen = find_product(session, query)
    if not product:
        print(f"  [skip] nothing on Mixamo matches {query!r}")
        if seen:
            print("         closest results: " + ", ".join(dict.fromkeys(seen)))
            print(f'         pick one with:  --clip {out_name}="<name>"')
        return False
    label = product["name"]
    print(f"  using Mixamo animation '{label}'")

    # Product details carry the motion params ("gms_hash") needed to export.
    pid = product["id"]
    d = request(session, "GET", API + f"/products/{pid}", params={"similar": 0, "char_id": character_id})
    d.raise_for_status()
    gms = d.json()["details"]["gms_hash"]

    # params is a list like [["Posture", 0, ...], ["Arm Space", 0, ...]]; export wants
    # the default values joined ("0,0"). In-place is its own flag, as the web app
    # sends it: {"model-id": ..., "params": "0,0", "inplace": false, ...}.
    gms = dict(gms)
    params = gms.get("params", [])
    if isinstance(params, list):
        gms["params"] = ",".join(str(p[1] if isinstance(p, (list, tuple)) else p) for p in params)
    gms["inplace"] = bool(in_place)

    body = {
        "gms_hash": [gms],
        "preferences": {"format": "fbx7_2019", "skin": "false", "fps": "30", "reducekf": "0"},
        "character_id": character_id,
        "type": "Motion",
        "product_name": label,
    }
    e = request(session, "POST", API + "/animations/export", json=body)
    if e.status_code not in (200, 202):  # 202 = queued, which is the normal answer
        print(f"  [fail] export '{label}' -> HTTP {e.status_code}: {e.text[:200]}")
        return False
    job_uuid = None
    try:
        job_uuid = e.json().get("job_uuid")
    except ValueError:
        pass

    url = wait_for_job(session, character_id, job_uuid, label)
    if not url:
        return False
    # The result is a pre-signed S3 link: its auth is in the query string, and S3
    # rejects a second (our Bearer) Authorization header with 400. Fetch it bare.
    data = requests.get(url, timeout=120)
    data.raise_for_status()
    path = os.path.join(OUT_DIR, out_name + ".fbx")
    with open(path, "wb") as f:
        f.write(data.content)
    print(f"  [ok]   {out_name}.fbx  ({len(data.content)//1024} KB)  <- '{label}'")
    return True


def main():
    ap = argparse.ArgumentParser(description="Download robot-showcase animations from Mixamo.")
    ap.add_argument("--token", default=os.environ.get("MIXAMO_TOKEN"))
    ap.add_argument("--character-id", default=os.environ.get("MIXAMO_CHARACTER_ID"))
    ap.add_argument("--all", action="store_true", help="also fetch turn/crouch/rifle clips")
    ap.add_argument("--pistol", action="store_true", help="also fetch the pistol clips (Robot Arena)")
    ap.add_argument("--turns", action="store_true", help="also fetch turn_left / turn_right (turning in place)")
    ap.add_argument("--search", metavar="QUERY", help="list Mixamo animations matching QUERY and exit")
    ap.add_argument("--force", action="store_true", help="download clips even if the .fbx already exists")
    ap.add_argument("--clip", action="append", default=[], metavar='NAME="Mixamo Name"',
                    help='use this exact Mixamo animation for a clip, e.g. --clip pistol_idle="Pistol Idle"')
    args = ap.parse_args()

    if args.search:
        if not args.token:
            sys.exit("--search needs MIXAMO_TOKEN (or --token).")
        session = requests.Session()
        session.headers.update(headers(args.token))
        for p in search_products(session, args.search, limit=24):
            print(f"  {p['name']}  ({p.get('description', '')})")
        return

    if not args.token or not args.character_id:
        sys.exit("Set MIXAMO_TOKEN and MIXAMO_CHARACTER_ID (env vars or --token/--character-id).\n"
                 "See the comment at the top of this file for how to get them.")

    clips = dict(CORE_CLIPS)
    if args.all:
        clips.update(EXTRA_CLIPS)
    if args.pistol or args.all:
        clips.update(PISTOL_CLIPS)
    if args.turns or args.all:
        clips.update(TURN_CLIPS)
    for override in args.clip:
        name, _, query = override.partition("=")
        if not query:
            sys.exit(f'--clip expects NAME="Mixamo Name", got {override!r}')
        in_place = clips.get(name, (None, True))[1]
        clips[name] = (query.strip('"'), in_place)

    # Keep clips that are already here (re-download with --force).
    if not args.force:
        have = [n for n in clips if os.path.exists(os.path.join(OUT_DIR, n + ".fbx"))]
        for n in have:
            del clips[n]
        if have:
            print("Already present (use --force to replace): " + ", ".join(have))

    session = requests.Session()
    session.headers.update(headers(args.token))

    print(f"Downloading {len(clips)} clip(s) into {OUT_DIR}")
    ok = 0
    for out_name, (query, in_place) in clips.items():
        print(f"- {out_name}")
        try:
            if export_and_download(session, args.character_id, query, out_name, in_place):
                ok += 1
        except requests.HTTPError as ex:
            code = ex.response.status_code if ex.response is not None else None
            host = ex.request.url.split("/")[2] if ex.request is not None and ex.request.url else "?"
            hint = "  (token expired? re-copy it from the browser)" if code in (401, 403) else ""
            print(f"  [fail] {out_name}: HTTP {code} from {host}{hint}")
        except Exception as ex:  # noqa
            print(f"  [fail] {out_name}: {ex}")
    print(f"\nDone: {ok}/{len(clips)} downloaded. Rebuild, then reload the showcase.")


if __name__ == "__main__":
    main()
