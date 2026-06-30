Walking robot — how to get a rigged + animated model
=====================================================

The "Load Robot Showcase" scene loads:  assets/models/robot.fbx
(relative to the executable; the build copies assets/ next to the binary, so just
drop the file here and rebuild — or copy it into build/Sandbox/assets/models/).

The engine reads the skeleton + animation embedded in the SAME file with Assimp,
so you want a single FBX that has BOTH the skinned mesh and the walk animation.

Easiest source: Mixamo (free, by Adobe)
----------------------------------------
1. Go to https://www.mixamo.com  and sign in with a free Adobe account.
2. Pick a Character (e.g. "X Bot" / "Y Bot" — these are simple robots), OR upload
   your own. The character must be RIGGED (Mixamo auto-rigs uploads for you).
3. Click the "Animations" tab and search for "Walking". Pick any walk cycle.
4. Press DOWNLOAD and use these settings:
       Format .................. FBX Binary (.fbx)
       Skin .................... With Skin     <-- IMPORTANT (includes the mesh)
       Frames per Second ....... 30
       Keyframe Reduction ...... none
   (Leave "In Place" ON if you want it to walk without drifting away.)
5. Rename the downloaded file to  robot.fbx  and put it in THIS folder.
6. Rebuild (or just relaunch) and choose  File > Load Robot Showcase.

Other sources that also work (single file with skin + one animation):
  - Sketchfab (download as FBX/glTF, "with animation")
  - glTF sample models (e.g. CesiumMan.glb) — rename and update the path/extension
  - Any .fbx / .glb / .dae that contains a skinned mesh and at least one animation

Keyboard controller + extra animations (idle / run / jump)
----------------------------------------------------------
The Robot Showcase has a Lua controller on the robot: press Run, click the
viewport, then drive with the arrow keys (Up/Down walk, Left/Right turn, Shift to
run, Space to jump). With only robot.fbx it walks/idles using the single walk clip.
To get distinct turn/walk/run/jump animations, add these clips for the SAME
character and drop them in THIS folder with these EXACT names:

    idle.fbx     <- a "Idle" / "Breathing Idle" animation
    run.fbx      <- a "Running" animation
    jump.fbx     <- a "Jump" animation

How to download each from Mixamo:
  1. mixamo.com -> pick the SAME character you used for robot.fbx (so the
     skeleton/bone names match -- this is required for the clips to retarget).
  2. Animations tab -> search "Idle" (then "Running", then "Jump").
  3. DOWNLOAD as FBX Binary. Skin = "Without Skin" is fine and smaller (the
     skeleton + animation are still included); "With Skin" also works.
     Use "In Place" for run/idle so they don't drift.
  4. Save as idle.fbx / run.fbx / jump.fbx here, then reload the scene
     (File > Load Robot Showcase). The log prints "Merged 1 animation named ..."
     for each, and the controller automatically uses them.

The engine merges these onto robot.fbx's skeleton and names each clip after its
file (idle / run / jump). The controller checks HasAnimation(name) and falls back
to the walk clip when a file is missing, so you can add them one at a time.

Notes
-----
- Mixamo exports are in centimetres (~180 units tall). The showcase scales the
  entity to 0.02 so it fits; tweak Scale in the Transform panel for your model.
- Clips are selectable by name in the Timeline panel and the Animation component,
  and by name or index from Lua (CrossFade("run", 0.2), PlayAnimation("idle"), ...).
- glTF/glb and Collada (.dae) work too — just point the path at your file
  (edit BuildRobotShowcase() in Sandbox/MainLayer.cpp, or load via the editor).
