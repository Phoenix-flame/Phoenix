# Phoenix Engine

A real-time 3D game engine written in modern C++17, built for learning the internals
of a renderer/ECS/physics stack — inspired by [Hazel](https://github.com/TheCherno/Hazel).
It ships with **Sandbox**, a full editor application (ImGui-based) for building, editing,
playing, and serializing scenes.

> :warning: This project is under active development.

![The Phoenix editor showing the main showcase scene: neon cubes with bloom, shadows on the floor](images/editor.png)

---

## Table of Contents

- [Highlights](#highlights)
- [Feature Reference](#feature-reference)
  - [Entity Component System](#entity-component-system)
  - [Rendering](#rendering)
  - [Lighting & Shadows](#lighting--shadows)
  - [Post-Processing (Bloom / Glow)](#post-processing-bloom--glow)
  - [Linear Lighting (sRGB)](#linear-lighting-srgb)
  - [Mesh Loading](#mesh-loading)
  - [Skeletal Animation](#skeletal-animation)
  - [Primitive Shapes](#primitive-shapes)
  - [Physics](#physics)
  - [Terrain](#terrain)
  - [Water](#water)
  - [Lua Scripting](#lua-scripting)
  - [Scene Serialization](#scene-serialization)
  - [The Editor](#the-editor)
- [Built-in Showcases](#built-in-showcases)
- [Components at a Glance](#components-at-a-glance)
- [Building](#building)
- [Project Structure](#project-structure)
- [Tech Stack](#tech-stack)
- [Resources](#resources)

---

## Highlights

- **Entity Component System** powered by [EnTT](https://github.com/skypjack/entt).
- **Forward renderer** (OpenGL 4.5 with GLSL ES 3.00 shaders) with a geometry-agnostic
  submission API.
- **Phong lighting**: global ambient, up to eight directional lights, up to four
  point lights, per-material emissive/reflectivity, diffuse + normal maps.
- **Multi-light directional shadow mapping** with hardware PCF for smooth, unbanded soft shadows.
- **HDR bloom / neon glow** pipeline with ACES tonemapping and exposure control.
- **Opt-in linear lighting**: sRGB-correct textures, colours and output.
- **Procedural environment reflections** (sky/ground) per material.
- **Skeletal animation** — GPU vertex skinning, loop modes, crossfades and events; loads
  rigged/animated models (FBX, glTF, Collada, …) via Assimp.
- **Rigid-body physics** via [Jolt](https://github.com/jrouwe/JoltPhysics): box, sphere,
  capsule, cylinder, convex-hull and triangle-mesh colliders (combinable into compound
  bodies), six joint types with motors, springs and break forces, a character controller,
  force fields, collision layers, ray/sphere casts and overlap queries, and collider
  debug drawing, **vehicles** (wheeled cars) and **ragdolls** (for rigged models and
  primitive dummies), on a fixed, deterministic timestep.
- **Editable terrain** with a Blender-style sculpt brush (raise / lower / smooth).
- **Animated transparent water** surfaces for lakes/oceans.
- **Lua scripting** with an in-editor, syntax-highlighted code editor that runs while playing.
- **Scene serialization** (YAML) plus snapshot-based **undo/redo**.
- **Full editor**: hierarchy, properties inspector, gizmos, click-to-pick, play/stop,
  animation timeline, asset import.

---

## Feature Reference

### Entity Component System

Scenes are EnTT registries; an `Entity` is a lightweight handle wrapping an
`entt::entity` plus a `Scene*`. Components are plain structs in
[`Component.h`](phoenix/include/Phoenix/Scene/Component.h). System logic runs inline in
`Scene::OnUpdate`. Create entities from code (`Scene::CreateEntity`) or from the editor
hierarchy panel.

### Rendering

The renderer ([`renderer.cpp`](phoenix/src/renderer/renderer.cpp)) exposes a
geometry-agnostic path:

```cpp
Renderer::BeginScene(projection, view, cameraPos);
Renderer::SetLights(ambient, dirLights, dirLightDirs, numDirLights,
                    pointLights, pointLightPositions, numPointLights);
Renderer::Submit(vertexArray, material, transform, diffuseMap);   // any indexed mesh
Renderer::SubmitAnimated(vertexArray, material, transform, boneMatrices, diffuseMap);
Renderer::EndScene();
```

Vertices are interleaved `position / normal / texcoord / boneIDs / weights`. The
renderer also draws editor aids: selection **outlines** (stencil-based), **camera frustum
gizmos**, and **directional-light arrows**. A global **wireframe** toggle is available
per entity via the `WireframeComponent`. [`Example/`](Example/ExampleLayer.cpp) is a
one-file program that uses exactly this API to draw a lit, spinning cube.

<img src="images/example.png" alt="The minimal Example app: a lit orange cube with a small settings panel" width="480">

Textures loaded from disk get a full mip chain with trilinear + anisotropic filtering, and
are cached by path so a model whose sub-meshes share a material uploads it once.

### Lighting & Shadows

- **Ambient**: a single global ambient colour (editable in Settings, serialized).
- **Directional lights** (`DirLightComponent`): up to 8; each aims along its entity's
  local −Z, visualized with an arrow gizmo, and can cast its own shadow.
- **Point lights** (`PointLightComponent`): up to 4, with constant/linear/quadratic
  attenuation.
- **Emissive objects also emit light**: any material with `emissiveStrength > 0` is added
  as a gentle coloured point light onto its surroundings (until the 4-light cap).
- **Shadows**: **multi-light** directional shadow mapping — up to 8 directional lights,
  each rendering a 1024² tile of one shared depth atlas from its POV, sampled with a 3×3
  kernel of **hardware PCF** taps (`sampler2DShadow`) for smooth edges. Toggle per light
  with *Casts Shadow*. Animated
  meshes are skinned in the shadow pass too, so a walking character casts a correctly posed
  shadow. (Point lights light the scene but don't cast shadows yet.)

### Post-Processing (Bloom / Glow)

An HDR pipeline ([`Bloom.cpp`](phoenix/src/renderer/Bloom.cpp)) renders the scene to an
`RGBA16F` target, extracts bright pixels (threshold) while downsampling to half
resolution, blurs them with a separable Gaussian (bilinear-tap, ping-pong), then
composites with **ACES** tonemapping and an **exposure** control. Push a material's
`emissive * emissiveStrength` above 1.0 to make it bloom — the basis of the neon-glow look.
All knobs (intensity, threshold, exposure) live in the editor Settings panel.

### Linear Lighting (sRGB)

Off by default, so existing scenes keep their look. Turn on **Linear Lighting (sRGB)** in
Settings (or `Renderer::SetLinearLighting(true)`) to light in linear space:

- diffuse maps are sampled through an sRGB texture view (hardware-decoded to linear);
  normal maps stay linear;
- colours picked in the editor — materials, emissive, lights, ambient, water, background —
  are treated as sRGB and converted to linear before shading;
- the composite pass encodes the tonemapped result back to sRGB. The HDR path is used
  whenever linear lighting is on, even with bloom off.

Light falloff, blending and bloom are then physically correct. Scenes tuned without it
usually need their lights and materials re-balanced.

![The showcase scene with linear lighting off and on](images/linear-lighting.png)

### Mesh Loading

Models are loaded with **Assimp** ([`Mesh.cpp`](phoenix/src/renderer/Mesh.cpp)) and support
**diffuse textures**; any material can also take a diffuse and normal map from the
editor's **Textures** panel. Loading is **asynchronous**: the file is parsed on a worker thread and
the GPU buffers/textures are uploaded on the main thread in `Model::Update()`, so the editor
never blocks on a slow import. Add via the editor (Mesh component) or `CreateRef<Model>(path)`.

### Skeletal Animation

Full GPU vertex skinning ([`Animation.h`](phoenix/include/Phoenix/renderer/Animation.h) /
[`Animation.cpp`](phoenix/src/renderer/Animation.cpp)):

- The model loader extracts the **bone hierarchy**, per-vertex **bone weights** (up to 4),
  and all **embedded animation clips**.
- Each clip's node hierarchy is flattened once at load time (parent before child, with
  its channel and bone slot resolved), so posing a skeleton does no string lookups.
- `Bone` samples keyframes with a binary search (lerp position/scale, slerp rotation);
  `Animator` walks the flattened skeleton each frame and produces up to `MAX_BONES` (100)
  final matrices.
- **Crossfades** blend each joint's translation / rotation / scale (not the final
  matrices, which would shrink and shear limbs mid-blend). Joints are matched by name, so
  clips merged from other files blend too. Interrupting a fade continues from the pose on
  screen instead of popping.
- **Loop modes** (Loop / Once / PingPong), speed, pause/seek, and named **events** at clip
  times that call the entity's Lua `OnAnimationEvent(name)`.
- The vertex shader blends the bone matrices by weight (`u_BoneMatrices[]`, gated by
  `u_Animated`), in both the lit pass and the shadow pass.
- Drive it with an `AnimationComponent` (`clip`, `playing`, `speed`, `loopMode`,
  `crossfade`, `extraClips`, `events`), from Lua, or by scrubbing in the **Timeline** panel.
  `extraClips` merges animation-only files (e.g. Mixamo "without skin" exports) onto the rig.

![A rigged Mixamo character mid-walk, casting a skinned shadow](images/robot-showcase.png)

Works with any rigged + animated file Assimp can read (FBX, glTF/GLB, Collada/DAE, …).
See [Built-in Showcases](#built-in-showcases) for the walking-robot demo and how to download
a free rigged character + walk cycle from Mixamo.

### Primitive Shapes

Built-in procedural meshes ([`Primitives.cpp`](phoenix/src/renderer/Primitives.cpp)) —
**Cube, Sphere, Cylinder (pillar), Cone, Plane** — exposed through a `PrimitiveComponent`
(type + material). Meshes are unit-sized and cached/shared across entities. They render with
the full material pipeline (emissive, reflectivity, wireframe), cast shadows, show selection
outlines, and (with a Rigid Body + Mesh Collider) collide as convex hulls or triangle meshes.
Create from the hierarchy panel's **Create Shape** menu or **Add Component → Shape**.

### Physics

Simulation runs on **Jolt Physics** (v5.5, multithreaded) behind a pimpl wrapper,
[`PhysicsWorld`](phoenix/include/Phoenix/Physics/Physics.h), so no other engine code
includes Jolt. Press **Play** to build the physics world from the scene; transforms are
written back to entities every frame. **Stop** restores the edit-time state.

By default it advances in **fixed steps** (60 Hz, in the Physics panel). Rendering
interpolates between the last two steps, so motion stays smooth at any frame rate, and
the same inputs give bit-identical results at 20 or 144 fps. Turn *Fixed Timestep* off
for one variable step per frame.

**Moving things while playing.** Drag a body with the gizmo (or `SetTranslation` it from
Lua) and it teleports there and drops from rest when released. The engine notices any
transform change it didn't make itself. This works for static and dynamic bodies and
characters.

**Making an object physical.** Right-click the hierarchy, then **Create Physics Object**
for ready-made bodies: box, sphere, barrel, box pyramid, floor, ramp, moving platform,
trigger zone, wind zone, black hole, chain, hinged door, motorized wheel, ragdoll dummy,
car, or a player character. To build one by hand, add a **Rigid Body** and at least one **collider**. Each
collider has a **Fit to Shape** button that sizes it to the mesh or primitive.

- `RigidBodyComponent`: Static, Dynamic or Kinematic (kinematic bodies follow their
  Transform and push dynamic bodies). It holds:
  - friction and restitution, with **material presets** (wood, metal, ice, rubber…)
  - density or a **mass override**, plus a **center-of-mass offset**
  - linear and angular damping, a gravity factor (negative values float upward), and
    initial linear and angular velocity
  - **position and rotation axis locks** ("2D" and "Upright" presets)
  - continuous collision for fast movers, sleeping, sensor (trigger) mode, and a
    **collision layer**
- Colliders: `Box`, `Sphere`, `Capsule` and `Cylinder` each take a local **offset** and
  **rotation**. `CompoundColliderComponent` holds **any number** of those shapes (two
  boxes, a five-box table, …). Every collider on an entity is combined into one
  **compound** body.
  `MeshColliderComponent` builds a convex hull or an exact triangle mesh (static only)
  from the mesh. Terrain gets a static mesh collider automatically.
- `JointComponent` holds **a list of joints**: a plank hinged at both ends, a torso
  holding the head and both arms. Each joint connects the body to another body (by Tag)
  or to the world:
  - **Fixed**: welds two bodies.
  - **Point**: ball-and-socket.
  - **Distance**: rope or rod, with an optional **spring** (frequency and damping).
  - **Hinge**: angle limits, friction and a **velocity or position motor**.
  - **Slider**: travel limits, friction and a motor.
  - **Cone**: swing limit.
  - **Swing-Twist**: separate limits on two swing axes plus a twist range (ragdoll
    shoulders, hips, knees).

  Any joint can have a **break force**, and the two jointed bodies don't collide unless
  you tick *Collide Connected*.
- `CharacterControllerComponent`: a kinematic capsule (Jolt `CharacterVirtual`). It walks
  up steps, slides off slopes that are too steep, rides moving platforms and pushes
  bodies. Tick *Keyboard Control* for WASD/arrows relative to the camera, Shift to run
  and Space to jump, or drive it from Lua with `Move`/`Jump`.
- `VehicleComponent` turns a Dynamic rigid body into a four-wheeled **car**, using
  Jolt's wheeled vehicle:
  - suspension springs, engine and automatic gearbox
  - front-, rear- or all-wheel drive, anti-roll bars, brakes and handbrake
  - optional wheel entities, posed every frame (the built-in cylinder works as a wheel)
  - *Keyboard Control*: W/S throttle and reverse, A/D steer, Space handbrake; or drive it
    from Lua with `SetVehicleInput`
  - Wheel layout is in meters in the car's frame; the car drives toward −Z.
- `RagdollComponent` turns a **rigged, animated model** into a ragdoll on demand:
  - *Start Active*, Lua `SetRagdoll(true)` or the inspector button builds capsule bodies
    along the humanoid bones from the **current pose**, joined by swing-twist joints, and
    the skinned mesh follows them.
  - `SetRagdoll(false)` returns to animation.
  - Bones are matched by name: Mixamo (`mixamorig:Hips`, `LeftUpLeg`, …) plus common
    `pelvis`/`thigh`/`calf`/`upperarm` variants.
  - For anything without a skeleton, **Create Physics Object → Ragdoll Dummy** builds an
    11-part ragdoll from primitive shapes.
- `ForceFieldComponent`: a box or sphere zone that applies a directional (wind,
  updraft) or radial (attract or repel) acceleration, plus optional drag, to every
  dynamic body inside it.
- **Physics panel**:
  - gravity presets, solver iterations and substep rate
  - **collision layers**: rename up to 16 layers and edit the collision matrix
  - debug draw toggles
  - live body, awake-body and joint counts
  - impulse buttons to poke the selected body while playing
- **Debug draw** ("Show Colliders") outlines every collider, color-coded: static,
  dynamic, sleeping, kinematic, sensor, character and force field each get their own
  color. It also shows joints (pivot, axis and link) and, optionally, recent contact
  points and normals.

### Terrain
### Terrain

`TerrainComponent` is an editable heightmap (resolution × resolution grid) rendered as a mesh
with analytic normals and an optional static triangle-mesh collider. A Blender-style
**sculpt tool** (raise / lower / smooth brush, configurable radius & strength) lets you carve
hills, valleys, and riverbeds directly in the viewport.

### Water

`WaterComponent` is an animated, transparent surface (a flat grid displaced by a sum-of-sines
wave in the shader) with Fresnel environment reflection, sun specular, and configurable
colour/alpha/amplitude/wave-scale/speed. Place one over a sculpted terrain basin to make a
lake.

### Lua Scripting

`LuaScriptComponent` holds a Lua source string that runs while the scene is **playing**
(per-entity, sandboxed `lua_State`: only the base, table, string, math and utf8 libraries
are loaded — no `io`/`os`). The API covers:

- **Transform**: `GetTranslation`, `SetTranslation`, `GetRotation`, `SetRotation`,
  `SetScale`, `Translate`, `Rotate`, `MoveForward`
- **Look**: `SetColor`, `SetEmissive`
- **Animation**: `PlayAnimation`, `CrossFade`, `Pause/Resume/StopAnimation`,
  `Get/SetAnimationTime`, `GetAnimationDuration`, `SetAnimationSpeed`, `SetAnimationLoop`,
  `IsAnimationPlaying`, `IsAnimationFinished`, `GetAnimationName`, `HasAnimation`
- **Physics: bodies**: `ApplyImpulse`, `ApplyImpulseAtPoint`, `ApplyAngularImpulse`,
  `ApplyForce`, `ApplyForceAtPoint`, `ApplyTorque`, `Get/SetVelocity`,
  `Get/SetAngularVelocity`, `GetMass`, `SetGravityFactor`, `SetFriction`,
  `SetRestitution`, `WakeUp`, `IsSleeping`, `Teleport(x,y,z [,rx,ry,rz])` (moves the
  body too; a plain `SetTranslation` on a dynamic body is overwritten by the simulation)
- **Physics: queries** (they never hit the caster or sensors; an optional last argument
  is a layer bit mask such as `1 << 2`):
  - `RayCast(o, dir, maxDist)` and `SphereCast(o, dir, radius, maxDist)` return
    `tag, x,y,z, nx,ny,nz, distance`, or `nil` when nothing is hit.
  - `OverlapSphere(x,y,z, r)` and `OverlapBox(x,y,z, hx,hy,hz)` return a table of tags.
- **Character**: `Move(vx, vz)`, `Jump([speed])`, `IsGrounded()`
- **Joints** (optional joint number, 1-based, as the last argument):
  `SetMotor("velocity"|"position"|"off", target)`, `SetMotorTarget(t)`, `GetJointValue()`
  (hinge angle, slider position or distance), `BreakJoint()`, `IsJointBroken()`,
  `GetJointCount()`
- **Vehicle**: `SetVehicleInput(forward, right [, brake, handbrake])`, `GetVehicleSpeed()`,
  `GetEngineRPM()` → rpm, gear
- **Ragdoll**: `SetRagdoll(on)`, `IsRagdoll()`, `RagdollImpulse(vx, vy, vz)`
- **Callbacks**:
  - `OnCollisionEnter(other, px,py,pz, nx,ny,nz, speed)` and `OnCollisionExit(other)`
  - `OnTriggerEnter(other)` and `OnTriggerExit(other)` for sensors
  - `OnJointBreak(other)`
- **Input**: `IsKeyDown`, `IsKeyPressed` (`Key.A`–`Key.Z`, `Key.0`–`Key.9`, arrows, Space, …)

The editor
includes a **dedicated, syntax-highlighted code editor** window (Lua keyword/API colouring,
Tab autocompletion for built-in functions, manual caret rendering) so you can edit scripts
without leaving the engine.

### Scene Serialization

Scenes serialize to **YAML** (`.phx`) via `SceneSerializer` — every component, including
materials, terrain heightfields, water, primitives, lights, physics, animation config and
Lua source. The same machinery powers **snapshot-based undo/redo**, which reconciles the
live scene in place (so undoing an unrelated edit never reloads your meshes).

### The Editor

The Sandbox app provides:

- **Hierarchy** panel — create/delete/select entities; Create menu for lights, terrain,
  water, and shapes.
- **Properties** inspector — per-component panels with live editing.
- **Viewport** — ImGuizmo transform gizmos, click-to-pick selection, terrain sculpting.
- **Timeline** — pick a clip, play/pause/stop, loop mode, speed, and scrub the selected
  entity's animation, with its event markers on the ruler (events are added in the
  Animation component's properties).
- **Script Editor**, **Textures**, **Shader Library**, **Profiler** panels.
- **Settings** — ambient, linear lighting, bloom/exposure, sculpt brush, vsync.
- **Physics** — gravity/solver settings, collision layer matrix, collider debug draw, live
  stats and impulse buttons for the selected body.
- **File / Edit menus** — load showcases, import/export scenes & shaders, undo/redo.

**Controls**

| Action | Binding |
| --- | --- |
| Translate / Rotate / Scale gizmo | `W` / `E` / `R` |
| No gizmo | `Q` |
| Orbit camera | Right-mouse drag |
| Pan camera | Middle-mouse drag |
| Zoom | Mouse wheel |
| Select entity | Left-click in viewport |
| Deselect | `Esc` |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Shift+Z` |
| Play / Stop | toolbar button |

---

## Built-in Showcases

Open via the **File** menu:

- **Load Showcase Scene** — bloom, neon glow, emissive lighting, environment reflection,
  shadows, a textured backpack dropping with a convex collider, and a Lua-scripted cube.
- **Load Water Showcase** — a sculpted terrain lake basin filled with animated water and
  glowing buoys.
- **Load Robot Showcase** — skeletal animation: a rigged character on a shadow-receiving
  floor with a follow camera. Press **Run**, click the viewport, then drive it with the
  arrow keys (Shift runs, Space jumps); a Lua state machine crossfades idle / walk / run /
  jump. **G** turns the robot into a physics ragdoll and back; **H** shoves it.
- **Load Multi-Shadow Showcase** — three coloured directional lights, each casting its own
  shadow.
- **Load Physics Showcase** — restitution and friction tests, a jointed wrecking ball, a
  hinged seesaw, a Lua-driven kinematic spinner, a tethered balloon, a CCD cannon (press
  **F**) through a sensor gate, a motorized windmill, a slider elevator, a welded tower
  with breakable joints hit by a pendulum, an updraft force field, a compound hammer, a
  collision-layer "ghost" ball, and a keyboard-driven character (**WASD**, **Space**,
  **Shift**) with stairs. Turn on *Show Colliders* in the Physics panel to see it all.
- **Load Vehicle Showcase** — a test track with a chase camera and a drivable car
  (**W/S**, **A/D**, **Space** handbrake). It has a jump ramp, a cone slalom, a crate wall,
  crash-test ragdoll dummies, and a boom gate held by a hinge plus a spring (two joints
  on one entity).

### Getting a walking character for the Robot Showcase

The robot showcase loads `assets/models/robot.fbx`. To get a free rigged model + walk cycle:

1. Sign in at **[mixamo.com](https://www.mixamo.com)** (free Adobe account).
2. Pick a rigged character (e.g. *X Bot*) or upload your own (it auto-rigs).
3. **Animations** tab → search **"Walking"** → choose a walk cycle.
4. **Download** as **FBX Binary**, **Skin = With Skin** (bundles mesh + skeleton + animation),
   30 FPS. Keep *In Place* on so it walks without drifting.
5. Rename to `robot.fbx`, drop it in `Sandbox/assets/models/`, rebuild (or copy into
   `build/Sandbox/assets/models/`), then **File → Load Robot Showcase**.

glTF (`.glb`), Collada (`.dae`) and Sketchfab "with animation" exports also work. Full notes
in [`Sandbox/assets/models/README.txt`](Sandbox/assets/models/README.txt).

---

## Components at a Glance

| Component | Purpose |
| --- | --- |
| `TagComponent` | Entity name |
| `TransformComponent` | Translation / rotation / scale |
| `CameraComponent` | Scene camera (perspective/ortho), primary flag |
| `MeshComponent` | Assimp-loaded model + material |
| `AnimationComponent` | Skeletal animation playback, loop mode, crossfade, extra clips, events |
| `PrimitiveComponent` | Built-in shape (cube/sphere/cylinder/cone/plane) + material |
| `CameraFollowComponent` | Third-person camera that trails a tagged entity |
| `CubeComponent` | Built-in unit cube + material |
| `Material` | Ambient/diffuse/specular/shininess + emissive + reflectivity |
| `DirLightComponent` | Directional light (sun) |
| `PointLightComponent` | Point light with attenuation |
| `RigidBodyComponent` | Static / Dynamic / Kinematic body: material, mass, locks, layer, sensor |
| `Box/Sphere/Capsule/CylinderColliderComponent` | Primitive colliders with local offset/rotation (combine into compounds) |
| `MeshColliderComponent` | Convex-hull / triangle-mesh collider |
| `CompoundColliderComponent` | Any number of box / sphere / capsule / cylinder shapes |
| `JointComponent` | List of joints: fixed / point / distance (spring) / hinge / slider / cone / swing-twist; motors, limits, break force |
| `VehicleComponent` | Wheeled car: suspension, engine, gearbox, drive layout, wheel visuals |
| `RagdollComponent` | Skeleton ragdoll for rigged models, toggled at runtime |
| `CharacterControllerComponent` | Walking capsule character (steps, slopes, jumping, pushing) |
| `ForceFieldComponent` | Wind / updraft / attractor zone |
| `TerrainComponent` | Editable heightmap terrain |
| `WaterComponent` | Animated transparent water surface |
| `WireframeComponent` | Draw the entity as wireframe |
| `LuaScriptComponent` | Lua script run while playing |
| `NativeScriptComponent` | C++ scriptable entity |

---

## Building

Phoenix targets **Linux** and needs **OpenGL 4.5** and **CMake 3.16+**.

### Dependencies

Install via your system package manager (apt names shown):

```bash
sudo apt install build-essential cmake \
    libglfw3-dev libglew-dev libglm-dev libopengl-dev \
    libfmt-dev libspdlog-dev libassimp-dev libyaml-cpp-dev liblua5.3-dev
```

- **Jolt Physics** is fetched and built from source automatically by CMake (FetchContent).
- **EnTT** is vendored as a single header in `third_party/`.
- **ImGui / ImGuizmo** are vendored in the engine sources.

### Compile & Run

```bash
cmake -S . -B build              # defaults to RelWithDebInfo
cmake --build build -j$(nproc)

./build/Sandbox/Sandbox          # the editor
./build/Example/Example          # the minimal example
```

Pass `-DCMAKE_BUILD_TYPE=Debug` to enable engine asserts and the OpenGL debug-message
callback.

Assets are copied next to each executable on every build, so shader edits are picked up on
the next launch. The engine's own runtime assets (its shaders and the UI fonts) live in
`phoenix/assets/`; an app gets them with `phoenix_copy_assets(<target>)` in its
`CMakeLists.txt`, then copies its own assets on top. Apps load assets by relative path, so
`main()` changes to the executable's directory first (see `Example/main.cpp`).

---

## Project Structure

```
Phoenix/
├── phoenix/                  # the engine (static library)
│   ├── include/Phoenix/      # public headers
│   │   ├── core/  event/  renderer/  Scene/  Physics/  imGui/  Math/  opengl/
│   ├── src/                  # implementation (renderer, Scene, Physics, ...)
│   └── assets/               # engine shaders + UI fonts (copied next to every app)
├── Sandbox/                  # the editor application
│   ├── MainLayer.cpp         # viewport, showcases, gizmos, undo/redo, menus
│   ├── Panels/SceneEditor.*  # hierarchy, properties, timeline, Lua editor panels
│   ├── assets/               # showcase models, textures, extra shaders
│   └── backpack/             # textured backpack model for the main showcase
├── Example/                  # minimal app: a lit cube through the Renderer API
├── third_party/entt/         # vendored EnTT single header
├── images/                   # screenshots
└── CMakeLists.txt
```

---

## Tech Stack

| Area | Library |
| --- | --- |
| Windowing / input | GLFW |
| GL loading | GLEW |
| Math | GLM |
| ECS | EnTT (vendored) |
| Physics | Jolt Physics (fetched) |
| Model import | Assimp |
| Scripting | Lua 5.3 |
| Serialization | yaml-cpp |
| UI | Dear ImGui + ImGuizmo (vendored) |
| Logging | spdlog / fmt |

---

## Resources

- [The Book of Shaders](https://thebookofshaders.com/)
- [Learn OpenGL](https://learnopengl.com/Getting-started)
- [Hazel Engine](https://github.com/TheCherno/Hazel) (inspiration)
