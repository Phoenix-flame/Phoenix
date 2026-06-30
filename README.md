# Phoenix Engine

A real-time 3D game engine written in modern C++17, built for learning the internals
of a renderer/ECS/physics stack — inspired by [Hazel](https://github.com/TheCherno/Hazel).
It ships with **Sandbox**, a full editor application (ImGui-based) for building, editing,
playing, and serializing scenes.

> :warning: This project is under active development.

![Phoenix](https://github.com/Phoenix-flame/Phoenix/blob/master/images/new_ver.png?raw=true)

---

## Table of Contents

- [Highlights](#highlights)
- [Feature Reference](#feature-reference)
  - [Entity Component System](#entity-component-system)
  - [Rendering](#rendering)
  - [Lighting & Shadows](#lighting--shadows)
  - [Post-Processing (Bloom / Glow)](#post-processing-bloom--glow)
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
- **Forward renderer** (OpenGL core with GLSL ES 3.00 shaders) with a geometry-agnostic
  submission API.
- **PBR-ish Blinn–Phong lighting**: global ambient, one directional light, up to four
  point lights, per-material emissive/reflectivity.
- **Directional shadow mapping** with hardware PCF for smooth, unbanded soft shadows.
- **HDR bloom / neon glow** pipeline with ACES tonemapping and exposure control.
- **Procedural environment reflections** (sky/ground) per material.
- **Skeletal animation** — GPU vertex skinning, loads rigged/animated models (FBX, glTF,
  Collada, …) via Assimp.
- **Rigid-body physics** via [Jolt](https://github.com/jrouwe/JoltPhysics): box, convex-hull,
  triangle-mesh and heightfield colliders.
- **Editable terrain** with a Blender-style sculpt brush (raise / lower / smooth).
- **Animated transparent water** surfaces for lakes/oceans.
- **Lua scripting** with an in-editor, syntax-highlighted code editor that runs while playing.
- **Scene serialization** (YAML) plus snapshot-based **undo/redo**.
- **Full editor**: hierarchy, properties inspector, gizmos, click-to-pick, play/stop,
  asset import.

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
Renderer::SetLights(ambient, dirExists, dirLight, dirDir, pointLights, pointPos, count);
Renderer::Submit(vertexArray, material, transform, diffuseMap);   // any indexed mesh
Renderer::SubmitAnimated(vertexArray, material, transform, boneMatrices, diffuseMap);
Renderer::EndScene();
```

Vertices are interleaved `position / normal / texcoord / boneIDs / weights`. The
renderer also draws editor aids: selection **outlines** (stencil-based), **camera frustum
gizmos**, and **directional-light arrows**. A global **wireframe** toggle is available
per entity via the `WireframeComponent`.

### Lighting & Shadows

- **Ambient**: a single global ambient colour (editable in Settings, serialized).
- **Directional light** (`DirLightComponent`): the "sun"; its aim is the entity's local
  −Z, visualized with an arrow gizmo. Casts shadows.
- **Point lights** (`PointLightComponent`): up to 4, with constant/linear/quadratic
  attenuation.
- **Emissive objects also emit light**: any material with `emissiveStrength > 0` is added
  as a gentle coloured point light onto its surroundings (until the 4-light cap).
- **Shadows**: **multi-light** directional shadow mapping — up to 4 directional lights,
  each with its own 2048² depth map rendered from its POV and sampled with **hardware PCF**
  (`sampler2DShadow`) for smooth edges. Toggle per light with *Casts Shadow*. Animated
  meshes are skinned in the shadow pass too, so a walking character casts a correctly posed
  shadow. (Point lights light the scene but don't cast shadows yet.)

### Post-Processing (Bloom / Glow)

An HDR pipeline ([`Bloom.cpp`](phoenix/src/renderer/Bloom.cpp)) renders the scene to an
`RGBA16F` target, extracts bright pixels (threshold), blurs them through a 10-pass
ping-pong Gaussian, then composites with **ACES** tonemapping and an **exposure** control.
Push a material's `emissive * emissiveStrength` above 1.0 to make it bloom — the basis of
the neon-glow look. All knobs (intensity, threshold, exposure) live in the editor Settings
panel.

### Mesh Loading

Models are loaded with **Assimp** ([`Mesh.cpp`](phoenix/src/renderer/Mesh.cpp)) and support
**diffuse textures**. Loading is **asynchronous**: the file is parsed on a worker thread and
the GPU buffers/textures are uploaded on the main thread in `Model::Update()`, so the editor
never blocks on a slow import. Add via the editor (Mesh component) or `CreateRef<Model>(path)`.

### Skeletal Animation

Full GPU vertex skinning ([`Animation.h`](phoenix/include/Phoenix/renderer/Animation.h) /
[`Animation.cpp`](phoenix/src/renderer/Animation.cpp)):

- The model loader extracts the **bone hierarchy**, per-vertex **bone weights** (up to 4),
  and all **embedded animation clips**.
- `Bone` interpolates keyframes (lerp position/scale, slerp rotation); `Animator` walks the
  skeleton each frame and produces up to `MAX_BONES` (100) final matrices.
- The vertex shader blends the bone matrices by weight (`u_BoneMatrices[]`, gated by
  `u_Animated`), in both the lit pass and the shadow pass.
- Drive it with an `AnimationComponent` (`clip`, `playing`, `speed`).

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

Rigid-body simulation via **Jolt Physics** (pimpl wrapper in
[`Physics`](phoenix/src/Physics)):

- `RigidBodyComponent` — Static / Dynamic / Kinematic.
- Colliders: **Box** (`BoxColliderComponent`), **Convex hull** & **triangle mesh**
  (`MeshColliderComponent`, from the entity's mesh/primitive geometry), and a **heightfield**
  collider auto-generated for terrain.
- Press **Play** to start the simulation; transforms are written back to entities every
  frame. Press **Stop** to restore the edit-time state.

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
(per-entity `lua_State`). Scripts can read/modify their entity — e.g. `GetTranslation()`,
`SetTranslation(x,y,z)`, `SetColor(r,g,b)` — for movement, colour cycling, etc. The editor
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
- **Settings** — ambient, bloom/exposure, sculpt brush, vsync.
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
| Undo / Redo | `Ctrl+Z` / `Ctrl+Shift+Z` |
| Play / Stop | toolbar button |

---

## Built-in Showcases

Open via the **File** menu:

- **Load Showcase Scene** — bloom, neon glow, emissive lighting, environment reflection,
  shadows, a textured backpack dropping with a convex collider, and a Lua-scripted cube.
- **Load Water Showcase** — a sculpted terrain lake basin filled with animated water and
  glowing buoys.
- **Load Robot Showcase** — skeletal animation: a rigged character walking on a
  shadow-receiving floor.

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
| `AnimationComponent` | Skeletal animation playback (clip / playing / speed) |
| `PrimitiveComponent` | Built-in shape (cube/sphere/cylinder/cone/plane) + material |
| `CubeComponent` | Built-in unit cube + material |
| `Material` | Ambient/diffuse/specular/shininess + emissive + reflectivity |
| `DirLightComponent` | Directional light (sun) |
| `PointLightComponent` | Point light with attenuation |
| `RigidBodyComponent` | Static / Dynamic / Kinematic body |
| `BoxColliderComponent` | Box collider |
| `MeshColliderComponent` | Convex-hull / triangle-mesh collider |
| `TerrainComponent` | Editable heightmap terrain |
| `WaterComponent` | Animated transparent water surface |
| `WireframeComponent` | Draw the entity as wireframe |
| `LuaScriptComponent` | Lua script run while playing |
| `NativeScriptComponent` | C++ scriptable entity |

---

## Building

Phoenix targets **Linux**.

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
mkdir -p build && cd build
cmake ..
cmake --build . -j$(nproc)

# Run the editor
./Sandbox/Sandbox
```

Assets (shaders, models, textures) are copied next to the executable on every build, so
edits to shaders/scenes are picked up automatically on the next launch.

---

## Project Structure

```
Phoenix/
├── phoenix/                  # the engine (static library)
│   ├── include/Phoenix/      # public headers
│   │   ├── core/  event/  renderer/  Scene/  Physics/  imGui/  Math/  opengl/
│   └── src/                  # implementation (renderer, Scene, Physics, ...)
├── Sandbox/                  # the editor application
│   ├── MainLayer.cpp         # viewport, showcases, gizmos, undo/redo, menus
│   ├── Panels/SceneEditor.*  # hierarchy + properties + Lua editor panels
│   └── assets/               # shaders, models, textures (copied next to the exe)
├── Example/                  # a minimal example app
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
