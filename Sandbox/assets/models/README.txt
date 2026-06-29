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

Notes
-----
- Mixamo exports are in centimetres (~180 units tall). The showcase scales the
  entity to 0.02 so it fits; tweak Scale in the Transform panel for your model.
- If the model has several clips, change "Clip" in the Animation component.
- glTF/glb and Collada (.dae) work too — just point the path at your file
  (edit BuildRobotShowcase() in Sandbox/MainLayer.cpp, or load via the editor).
