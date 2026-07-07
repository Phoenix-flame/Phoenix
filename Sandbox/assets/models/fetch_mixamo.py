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

  Add --all to also fetch the optional turn/crouch/rifle set.
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

# output filename (== engine clip name)  ->  (Mixamo search query, force in-place)
CORE_CLIPS = {
    "idle": ("Breathing Idle", True),
    "walk": ("Walking", True),
    "run":  ("Running", True),
    "jump": ("Jump", False),
}
EXTRA_CLIPS = {
    "turn_left":    ("Left Turn", True),
    "turn_right":   ("Right Turn", True),
    "crouch_idle":  ("Crouching Idle", True),
    "crouch_walk":  ("Crouched Walking", True),
    "rifle_idle":   ("Rifle Aiming Idle", True),
    "rifle_walk":   ("Walk With Rifle", True),
    "rifle_run":    ("Jog Forward (Rifle)", True),
    "rifle_jump":   ("Rifle Jump", False),
    "rifle_crouch": ("Rifle Crouching Idle", True),
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


def find_product(session, query):
    """Return the first Motion product matching `query`, or None."""
    r = session.get(API + "/products", params={
        "page": 1, "limit": 12, "order": "", "type": "Motion,MotionPack", "query": query,
    })
    r.raise_for_status()
    results = r.json().get("results", [])
    return results[0] if results else None


def export_and_download(session, character_id, query, out_name, in_place):
    product = find_product(session, query)
    if not product:
        print(f"  [skip] no Mixamo result for '{query}'")
        return False

    # Product details carry the motion params ("gms_hash") needed to export.
    pid = product["id"]
    d = session.get(API + f"/products/{pid}", params={"similar": 0, "char_id": character_id})
    d.raise_for_status()
    gms = d.json()["details"]["gms_hash"]

    # params is a list like [["Overdrive", 0, ...], ["InPlace", 0, ...], ...]; export
    # wants the default values joined. Force the in-place param on for locomotion.
    param_names = [p[0] for p in gms["params"]]
    param_vals = [p[1] for p in gms["params"]]
    if in_place:
        for i, name in enumerate(param_names):
            if "in place" in str(name).lower() or "inplace" in str(name).lower():
                param_vals[i] = 1
    gms = dict(gms)
    gms["params"] = ",".join(str(v) for v in param_vals)

    body = {
        "gms_hash": [gms],
        "preferences": {"format": "fbx7_2019", "skin": "false", "fps": "30", "reducekf": "0"},
        "character_id": character_id,
        "type": "Motion",
        "product_name": product["name"],
    }
    e = session.post(API + "/animations/export", json=body)
    if e.status_code != 200:
        print(f"  [fail] export '{query}' -> HTTP {e.status_code}: {e.text[:200]}")
        return False

    # Poll the per-character monitor until the job finishes, then download.
    url = None
    for _ in range(60):
        time.sleep(2)
        m = session.get(API + f"/characters/{character_id}/monitor")
        if m.status_code != 200:
            continue
        js = m.json()
        status = js.get("status")
        if status == "completed":
            url = js.get("job_result")
            break
        if status == "failed":
            print(f"  [fail] Mixamo reported the export of '{query}' failed")
            return False
    if not url:
        print(f"  [fail] timed out waiting for '{query}'")
        return False

    data = session.get(url)
    data.raise_for_status()
    path = os.path.join(OUT_DIR, out_name + ".fbx")
    with open(path, "wb") as f:
        f.write(data.content)
    print(f"  [ok]   {out_name}.fbx  ({len(data.content)//1024} KB)  <- '{query}'")
    return True


def main():
    ap = argparse.ArgumentParser(description="Download robot-showcase animations from Mixamo.")
    ap.add_argument("--token", default=os.environ.get("MIXAMO_TOKEN"))
    ap.add_argument("--character-id", default=os.environ.get("MIXAMO_CHARACTER_ID"))
    ap.add_argument("--all", action="store_true", help="also fetch turn/crouch/rifle clips")
    args = ap.parse_args()

    if not args.token or not args.character_id:
        sys.exit("Set MIXAMO_TOKEN and MIXAMO_CHARACTER_ID (env vars or --token/--character-id).\n"
                 "See the comment at the top of this file for how to get them.")

    clips = dict(CORE_CLIPS)
    if args.all:
        clips.update(EXTRA_CLIPS)

    session = requests.Session()
    session.headers.update(headers(args.token))

    print(f"Downloading {len(clips)} clip(s) into {OUT_DIR}")
    ok = 0
    for out_name, (query, in_place) in clips.items():
        print(f"- {query}")
        try:
            if export_and_download(session, args.character_id, query, out_name, in_place):
                ok += 1
        except requests.HTTPError as ex:
            print(f"  [fail] {query}: {ex}  (token expired? re-copy it from the browser)")
        except Exception as ex:  # noqa
            print(f"  [fail] {query}: {ex}")
    print(f"\nDone: {ok}/{len(clips)} downloaded. Reload File > Load Robot Showcase.")


if __name__ == "__main__":
    main()
