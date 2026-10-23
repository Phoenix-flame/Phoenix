#include "MainLayer.h"

#include <Phoenix/core/base.h>
#include <Phoenix/event/event.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/core/Profiler.h>
#include <Phoenix/imGui/ImGuiLayer.h>
#include "Panels/EditorUI.h"
#include "AIPrefabs.h"

#include <Phoenix/core/application.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/SceneSerializer.h>

#include <Phoenix/renderer/Framebuffer.h>
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/core/log.h>
#include <vendor/ImGuizmo/ImGuizmo.h>
#include <Phoenix/core/Input.h>
#include <Phoenix/Math/Math.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
MainLayer::MainLayer(const std::string& name): Phoenix::Layer(name), 
        m_MainCamera()
    { }

void MainLayer::OnAttach() {
    PHX_INFO("{0} attached.", this->layer_name);

    Application::Get().GetWindow().SetVSync(true);

    FramebufferSpecification fbSpec;
    fbSpec.Width = 640;
    fbSpec.Height = 480;
    m_Framebuffer = Framebuffer::Create(fbSpec);

    m_Scene = CreateRef<Phoenix::Scene>();

    m_Scene->OnResize(m_Framebuffer->GetSpecification().Width, m_Framebuffer->GetSpecification().Height);

    // The editor is an RL sandbox first: open on the learning showcase.
    BuildAIGoalShowcase();
    m_SceneName = "AI: Find the Goal";

    m_ShaderLibrary = CreateScope<ShaderLibrary>();
    m_ShaderLibrary->Add(Shader::Create("assets/shaders/basic.glsl"));
    m_ShaderLibrary->Add(Shader::Create("assets/shaders/lighting.glsl"));
    m_ShaderLibrary->Add(Shader::Create("assets/shaders/texture.glsl"));
    

    m_SceneEditor = CreateRef<SceneEditor>(m_Scene);

    m_Bloom.Init();

    m_LastSnapshot = SceneSerializer(m_Scene).SerializeToString();
}

void MainLayer::BuildShowcaseScene() {
    // Low ambient so shadows read clearly and the neon glow pops.
    m_Scene->AmbientColor() = glm::vec3(0.04f);

    {
        auto cam = m_Scene->CreateEntity("Camera");
        cam.AddComponent<CameraComponent>();
        cam.GetComponent<TransformComponent>().Translation = { 0.0f, 4.0f, 14.0f };
    }

    // Directional "sun" aimed down at an angle to cast clear shadows.
    {
        auto sun = m_Scene->CreateDirLightEntity("Sun");
        auto& t = sun.GetComponent<TransformComponent>();
        t.Translation = { 7.0f, 9.0f, 5.0f };
        t.Rotation = { glm::radians(-55.0f), glm::radians(35.0f), 0.0f };
        auto& dl = sun.GetComponent<DirLightComponent>();
        dl.ambient  = glm::vec3(0.04f);
        dl.diffuse  = glm::vec3(0.9f);
        dl.specular = glm::vec3(1.0f);
    }

    // Matte floor: receives shadows; also a static physics collider.
    {
        auto floor = m_Scene->CreateEntity("Floor");
        auto& c = floor.AddComponent<CubeComponent>();
        c.material.ambient = glm::vec3(0.5f);
        c.material.diffuse = glm::vec3(0.5f);
        c.material.specular = glm::vec3(0.1f);
        c.material.shininess = 8.0f;
        c.material.reflectivity = 0.25f; // subtle reflective sheen
        auto& t = floor.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, -1.0f, 0.0f };
        t.Scale = { 24.0f, 0.5f, 24.0f };
        floor.AddComponent<RigidBodyComponent>().type = RigidBodyComponent::Type::Static;
        floor.AddComponent<BoxColliderComponent>();
    }

    // Shadow-casting pillars.
    const glm::vec3 pillarPos[] = { {-5.0f, 1.5f, -3.0f}, {5.0f, 2.0f, -4.0f}, {-3.0f, 1.0f, 4.0f} };
    const float pillarHeight[] = { 3.0f, 4.0f, 2.0f };
    for (int i = 0; i < 3; i++){
        auto e = m_Scene->CreateEntity("Pillar " + std::to_string(i + 1));
        auto& c = e.AddComponent<CubeComponent>();
        c.material.ambient = glm::vec3(0.35f, 0.35f, 0.4f);
        c.material.diffuse = glm::vec3(0.35f, 0.35f, 0.4f);
        c.material.specular = glm::vec3(0.4f);
        c.material.shininess = 32.0f;
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = pillarPos[i];
        t.Scale = { 1.0f, pillarHeight[i], 1.0f };
    }

    // Neon glowing cubes: bloom + emit coloured light onto the scene.
    struct Neon { const char* name; glm::vec3 pos; glm::vec3 color; };
    const Neon neons[] = {
        { "Neon Magenta", { -4.0f, 2.5f,  0.0f }, { 1.0f, 0.0f, 1.0f } },
        { "Neon Cyan",    {  4.0f, 1.2f,  1.0f }, { 0.0f, 1.0f, 1.0f } },
        { "Neon Green",   {  0.0f, 3.5f, -3.0f }, { 0.2f, 1.0f, 0.2f } },
    };
    for (const auto& n : neons){
        auto e = m_Scene->CreateEntity(n.name);
        auto& c = e.AddComponent<CubeComponent>();
        c.material.ambient = n.color * 0.1f;
        c.material.diffuse = n.color;
        c.material.emissive = n.color;
        c.material.emissiveStrength = 4.0f;
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = n.pos;
        t.Scale = glm::vec3(0.6f);
    }

    // Glossy cube: strong specular highlight (closest thing to reflection we have).
    {
        auto e = m_Scene->CreateEntity("Mirror Cube");
        auto& c = e.AddComponent<CubeComponent>();
        c.material.ambient = glm::vec3(0.05f);
        c.material.diffuse = glm::vec3(0.15f, 0.15f, 0.2f);
        c.material.specular = glm::vec3(1.0f);
        c.material.shininess = 128.0f;
        c.material.reflectivity = 0.8f; // mirror-like environment reflection
        e.GetComponent<TransformComponent>().Translation = { 2.5f, 0.5f, 4.0f };
    }

    // Textured backpack: lit, casts a shadow, and drops with a convex mesh collider
    // (its actual geometry) onto the floor when you press Run.
    {
        auto bp = m_Scene->CreateEntity("Backpack");
        auto& mesh = bp.AddComponent<MeshComponent>();
        mesh.model = CreateRef<Model>("backpack/backpack.obj");
        bp.GetComponent<TransformComponent>().Translation = { 0.0f, 5.0f, 0.0f };
        bp.AddComponent<RigidBodyComponent>(); // dynamic
        bp.AddComponent<MeshColliderComponent>(); // convex hull of the mesh
    }

    // A Lua-scripted cube that orbits and colour-cycles while playing.
    {
        auto e = m_Scene->CreateEntity("Scripted Cube");
        auto& c = e.AddComponent<CubeComponent>();
        c.material.diffuse = glm::vec3(0.8f, 0.3f, 0.2f);
        e.GetComponent<TransformComponent>().Translation = { -2.0f, 1.0f, 3.0f };
        auto& script = e.AddComponent<LuaScriptComponent>();
        script.source =
            "local t = 0.0\n"
            "function OnUpdate(dt)\n"
            "    t = t + dt\n"
            "    SetTranslation(-2.0 + math.sin(t) * 2.0, 1.0 + 0.5 * math.sin(t * 2.0), 3.0)\n"
            "    SetColor(0.5 + 0.5 * math.sin(t), 0.5 + 0.5 * math.sin(t + 2.0), 0.5 + 0.5 * math.sin(t + 4.0))\n"
            "end\n";
    }

    // A glowing cube that drops when you press Run (physics + bloom).
    {
        auto box = m_Scene->CreateEntity("Falling Glow Cube");
        auto& c = box.AddComponent<CubeComponent>();
        c.material.diffuse = glm::vec3(1.0f, 0.6f, 0.1f);
        c.material.emissive = glm::vec3(1.0f, 0.6f, 0.1f);
        c.material.emissiveStrength = 3.0f;
        box.GetComponent<TransformComponent>().Translation = { 1.0f, 7.0f, 0.0f };
        box.AddComponent<RigidBodyComponent>();
        box.AddComponent<BoxColliderComponent>();
    }
}

void MainLayer::BuildWaterShowcase() {
    m_Scene->AmbientColor() = glm::vec3(0.08f, 0.09f, 0.12f);

    {
        auto cam = m_Scene->CreateEntity("Camera");
        cam.AddComponent<CameraComponent>();
        cam.GetComponent<TransformComponent>().Translation = { 0.0f, 10.0f, 34.0f };
    }

    // Sun
    {
        auto sun = m_Scene->CreateDirLightEntity("Sun");
        auto& t = sun.GetComponent<TransformComponent>();
        t.Translation = { 20.0f, 25.0f, 15.0f };
        t.Rotation = { glm::radians(-50.0f), glm::radians(30.0f), 0.0f };
        auto& dl = sun.GetComponent<DirLightComponent>();
        dl.ambient  = glm::vec3(0.05f);
        dl.diffuse  = glm::vec3(1.0f, 0.96f, 0.85f);
        dl.specular = glm::vec3(1.0f);
    }

    // Terrain carved into a lake basin (deep bowl in the centre, hills around the rim).
    {
        auto e = m_Scene->CreateEntity("Lake Terrain");
        auto& terr = e.AddComponent<TerrainComponent>();
        terr.resolution = 96;
        terr.size = 60.0f;
        terr.material.ambient   = glm::vec3(0.20f, 0.28f, 0.16f);
        terr.material.diffuse   = glm::vec3(0.32f, 0.44f, 0.24f);
        terr.material.specular  = glm::vec3(0.05f);
        terr.material.shininess = 6.0f;

        int N = terr.resolution;
        terr.heights.assign((size_t)N * N, 0.0f);
        for (int z = 0; z < N; z++){
            for (int x = 0; x < N; x++){
                float nx = ((float)x / (N - 1) - 0.5f) * 2.0f;
                float nz = ((float)z / (N - 1) - 0.5f) * 2.0f;
                float r = std::sqrt(nx * nx + nz * nz);
                float bowl   = -6.0f * (1.0f - glm::smoothstep(0.0f, 0.55f, r));
                float rim    = 3.5f * glm::smoothstep(0.45f, 0.9f, r);
                float ripple = 0.7f * std::sin(nx * 6.0f) * std::cos(nz * 6.0f) * glm::smoothstep(0.5f, 1.0f, r);
                terr.heights[(size_t)z * N + x] = bowl + rim + ripple;
            }
        }
    }

    // Water filling the basin up to y = -1.5: Gerstner waves + interactive ripples,
    // buoyancy coupling for the floating bodies below.
    {
        auto e = m_Scene->CreateEntity("Water");
        auto& w = e.AddComponent<WaterComponent>();
        w.size = 60.0f;
        w.resolution = 120;
        w.color = glm::vec3(0.04f, 0.20f, 0.30f);
        w.alpha = 0.72f;
        w.amplitude = 0.12f;
        w.waveScale = 0.5f;
        w.speed = 1.0f;
        w.choppiness = 0.7f;
        w.foam = 0.6f;
        w.interactiveRipples = true;
        w.buoyancy = 1.0f; // real-water density: wood/buoys float, the cannonball sinks
        w.linearDrag = 0.7f;
        w.angularDrag = 0.4f;
        e.GetComponent<TransformComponent>().Translation = { 0.0f, -1.5f, 0.0f };
    }

    // Glowing buoys: dynamic light bodies that bob on the waves and trail wakes
    // (press Run). They start above the surface and splash in.
    struct Buoy { const char* name; glm::vec3 pos; glm::vec3 color; };
    const Buoy buoys[] = {
        { "Buoy Red",  { -6.0f, 0.5f,  4.0f }, { 1.0f, 0.2f, 0.1f } },
        { "Buoy Blue", {  7.0f, 0.5f, -3.0f }, { 0.1f, 0.5f, 1.0f } },
    };
    for (const auto& b : buoys){
        auto e = m_Scene->CreateEntity(b.name);
        auto& c = e.AddComponent<CubeComponent>();
        c.material.diffuse = b.color;
        c.material.emissive = b.color;
        c.material.emissiveStrength = 4.0f;
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = b.pos;
        t.Scale = glm::vec3(0.5f);
        auto& rb = e.AddComponent<RigidBodyComponent>();
        rb.density = 250.0f; // light: floats high
        e.AddComponent<BoxColliderComponent>();
    }

    // Wooden crates dropped from height: splash ripples on entry, then float.
    const glm::vec3 cratePos[] = { { -2.0f, 4.0f, 1.0f }, { 1.5f, 6.0f, -2.0f }, { 3.5f, 8.0f, 3.0f } };
    for (int i = 0; i < 3; i++){
        auto e = m_Scene->CreateEntity("Crate " + std::to_string(i + 1));
        auto& c = e.AddComponent<CubeComponent>();
        c.material.ambient = glm::vec3(0.4f, 0.3f, 0.18f);
        c.material.diffuse = glm::vec3(0.72f, 0.54f, 0.32f);
        c.material.specular = glm::vec3(0.1f);
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = cratePos[i];
        t.Rotation = { 0.3f * i, 0.5f * i, 0.2f };
        t.Scale = glm::vec3(0.9f);
        auto& rb = e.AddComponent<RigidBodyComponent>();
        rb.density = 500.0f; // wood: floats half-submerged
        rb.friction = 0.5f;
        e.AddComponent<BoxColliderComponent>();
    }

    // A beach ball (very light, bobs high) and a cannonball (dense, splashes and
    // sinks to the lakebed — the terrain collider catches it).
    {
        auto e = m_Scene->CreateEntity("Beach Ball");
        auto& p = e.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere);
        p.material.ambient = glm::vec3(0.5f, 0.15f, 0.1f);
        p.material.diffuse = glm::vec3(1.0f, 0.35f, 0.25f);
        p.material.specular = glm::vec3(0.6f);
        p.material.shininess = 64.0f;
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = { -4.0f, 5.0f, -4.0f };
        t.Scale = glm::vec3(1.1f);
        auto& rb = e.AddComponent<RigidBodyComponent>();
        rb.density = 80.0f;
        rb.restitution = 0.3f;
        e.AddComponent<SphereColliderComponent>();
    }
    {
        auto e = m_Scene->CreateEntity("Cannonball");
        auto& p = e.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere);
        p.material.ambient = glm::vec3(0.1f);
        p.material.diffuse = glm::vec3(0.25f, 0.25f, 0.3f);
        p.material.specular = glm::vec3(0.8f);
        p.material.shininess = 96.0f;
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = { 5.0f, 7.0f, 5.0f };
        t.Scale = glm::vec3(0.8f);
        auto& rb = e.AddComponent<RigidBodyComponent>();
        rb.density = 4000.0f; // denser than water: sinks
        e.AddComponent<SphereColliderComponent>();
    }
}

void MainLayer::BuildRobotShowcase() {
    // Showcase for skeletal animation: a rigged character walking on a lit floor.
    m_Scene->AmbientColor() = glm::vec3(0.06f);

    {
        auto cam = m_Scene->CreateEntity("Camera");
        cam.AddComponent<CameraComponent>();
        auto& t = cam.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, 3.0f, 8.0f };
        t.Rotation = { glm::radians(-12.0f), 0.0f, 0.0f };
        // Third-person follow camera trailing the robot. Check "Primary" on its Camera
        // component (or uncheck to regain free editor navigation) to view through it.
        auto& follow = cam.AddComponent<CameraFollowComponent>();
        follow.target = "Walking Robot";
        follow.distance = 6.0f;
        follow.height = 3.0f;
        follow.lookHeight = 2.0f;
        follow.followYaw = true;     // camera stays behind the robot as it turns
        follow.modelForwardZ = true; // Mixamo robot mesh faces +Z
    }

    // Sun for clear shadows beneath the walker.
    {
        auto sun = m_Scene->CreateDirLightEntity("Sun");
        auto& t = sun.GetComponent<TransformComponent>();
        t.Translation = { 6.0f, 9.0f, 5.0f };
        t.Rotation = { glm::radians(-55.0f), glm::radians(30.0f), 0.0f };
        auto& dl = sun.GetComponent<DirLightComponent>();
        dl.ambient  = glm::vec3(0.05f);
        dl.diffuse  = glm::vec3(0.95f);
        dl.specular = glm::vec3(1.0f);
    }

    // Floor that receives the shadow.
    {
        auto floor = m_Scene->CreateEntity("Floor");
        auto& c = floor.AddComponent<CubeComponent>();
        c.material.ambient = glm::vec3(0.4f);
        c.material.diffuse = glm::vec3(0.45f);
        c.material.specular = glm::vec3(0.1f);
        c.material.shininess = 8.0f;
        auto& t = floor.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, -1.0f, 0.0f };
        t.Scale = { 24.0f, 0.5f, 24.0f };
        floor.AddComponent<RigidBodyComponent>().type = RigidBodyComponent::Type::Static;
        floor.AddComponent<BoxColliderComponent>();
    }

    // The walking robot. Drop a rigged + animated model at assets/models/robot.fbx
    // (see assets/models/README.txt for how to get one from Mixamo). Mixamo exports
    // are in centimetres (~180 units tall), so we scale way down. If the file is
    // missing the slot is simply empty; everything else still renders.
    {
        auto robot = m_Scene->CreateEntity("Walking Robot");
        auto& mesh = robot.AddComponent<MeshComponent>();
        mesh.model = CreateRef<Model>("assets/models/robot.fbx");
        mesh.material.ambient  = glm::vec3(0.2f);
        mesh.material.diffuse  = glm::vec3(0.7f, 0.7f, 0.75f);
        mesh.material.specular = glm::vec3(0.3f);
        mesh.material.shininess = 24.0f;
        auto& anim = robot.AddComponent<AnimationComponent>(); // clip 0 = robot.fbx's embedded clip
        anim.crossfade = 0.18f;
        // Named clips merged onto the rig if the files exist (clip name = filename).
        // Download them from Mixamo for the SAME character (see assets/models/README.txt).
        // walk.fbx is optional (robot.fbx already has a walk = clip 0); missing files are
        // skipped and the controller falls back to clip 0.
        anim.extraClips = { "assets/models/walk.fbx", "assets/models/idle.fbx",
                            "assets/models/run.fbx",  "assets/models/jump.fbx" };

        auto& t = robot.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, -0.75f, 0.0f };
        t.Scale = glm::vec3(0.02f); // tune in the Transform panel to fit your model

        // Keyboard controller (runs while playing). Press Run, click the viewport, then:
        //   Up/Down = walk fwd/back, Left/Right = turn, Shift = run, Space = jump.
        // Crossfades between the idle / walk / run / jump clips when present, and falls
        // back to clip 0 (the embedded walk) for any clip you haven't added yet.
        auto& script = robot.AddComponent<LuaScriptComponent>();
        script.source =
            "local WALK = 0           -- robot.fbx's embedded walk is clip 0\n"
            "local turn = 2.0         -- rad/sec\n"
            "local speed = 2.5        -- units/sec\n"
            "local FWD = -1.0         -- this mesh faces +Z; -1 makes Up walk that way\n"
            "local groundY = -0.75\n"
            "local vy = 0.0\n"
            "local onGround = true\n"
            "local state = ''\n"
            "\n"
            "-- use a named clip when it was loaded, else a fallback clip\n"
            "local function clipFor(name, fallback)\n"
            "    if HasAnimation(name) then return name else return fallback end\n"
            "end\n"
            "local function setState(s, clip)\n"
            "    if state ~= s then state = s; CrossFade(clip, 0.18); ResumeAnimation() end\n"
            "end\n"
            "\n"
            "function OnUpdate(dt)\n"
            "    if IsKeyDown(Key.Left)  then Rotate(0,  turn*dt, 0) end\n"
            "    if IsKeyDown(Key.Right) then Rotate(0, -turn*dt, 0) end\n"
            "    local run  = IsKeyDown(Key.LeftShift)\n"
            "    local fwd  = IsKeyDown(Key.Up)\n"
            "    local back = IsKeyDown(Key.Down)\n"
            "    if fwd  then MoveForward( FWD * speed*(run and 2.0 or 1.0)*dt) end\n"
            "    if back then MoveForward(-FWD * speed*dt) end\n"
            "\n"
            "    -- jump (simple script-side gravity; no physics body)\n"
            "    if IsKeyPressed(Key.Space) and onGround then\n"
            "        vy = 6.0; onGround = false\n"
            "        setState('jump', clipFor('jump', clipFor('walk', WALK)))\n"
            "    end\n"
            "    if not onGround then\n"
            "        local x, y, z = GetTranslation()\n"
            "        vy = vy - 12.0 * dt; y = y + vy * dt\n"
            "        if y <= groundY then y = groundY; vy = 0.0; onGround = true end\n"
            "        SetTranslation(x, y, z)\n"
            "        return -- hold the jump pose until we land\n"
            "    end\n"
            "\n"
            "    -- grounded locomotion: idle / walk / run\n"
            "    if fwd or back then\n"
            "        if run then\n"
            "            setState('run', clipFor('run', clipFor('walk', WALK)))\n"
            "            SetAnimationSpeed(HasAnimation('run') and 1.0 or 2.0)\n"
            "        else\n"
            "            setState('walk', clipFor('walk', WALK)); SetAnimationSpeed(1.0)\n"
            "        end\n"
            "    else\n"
            "        if HasAnimation('idle') then setState('idle', 'idle')\n"
            "        else state = 'idle'; PauseAnimation(); SetAnimationTime(0.0) end\n"
            "    end\n"
            "end\n";
    }
}

// A physics playground: bounce alley (restitution), friction ramp, wrecking ball
// (point-joint chain), seesaw (limited hinge), kinematic spinner, tethered balloon
// (distance joint + negative gravity), and a Space-fired CCD cannonball that flies
// through a sensor gate (Lua OnCollisionEnter/Exit) into a crate wall. Press Run.
void MainLayer::BuildPhysicsShowcase() {
    m_Scene->AmbientColor() = glm::vec3(0.07f);

    auto matte = [](Material& m, const glm::vec3& color){
        m.ambient = color * 0.5f;
        m.diffuse = color;
        m.specular = glm::vec3(0.2f);
        m.shininess = 16.0f;
    };

    {
        auto cam = m_Scene->CreateEntity("Camera");
        cam.AddComponent<CameraComponent>();
        auto& t = cam.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, 9.0f, 26.0f };
        t.Rotation = { -0.2f, 0.0f, 0.0f };
    }

    {
        auto sun = m_Scene->CreateDirLightEntity("Sun");
        auto& t = sun.GetComponent<TransformComponent>();
        t.Translation = { 10.0f, 14.0f, 8.0f };
        t.Rotation = { glm::radians(-55.0f), glm::radians(30.0f), 0.0f };
        auto& dl = sun.GetComponent<DirLightComponent>();
        dl.ambient = glm::vec3(0.05f);
        dl.diffuse = glm::vec3(0.85f);
    }

    // Arena floor.
    {
        auto floor = m_Scene->CreateEntity("Arena");
        auto& c = floor.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.42f, 0.44f, 0.48f));
        c.material.reflectivity = 0.12f;
        auto& t = floor.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, -0.25f, 0.0f };
        t.Scale = { 44.0f, 0.5f, 44.0f };
        auto& rb = floor.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Static;
        rb.friction = 0.8f;
        floor.AddComponent<BoxColliderComponent>();
    }

    // ---- Bounce alley: same drop, three restitutions ----
    {
        struct Bounce { const char* name; float x; float restitution; glm::vec3 color; };
        const Bounce balls[] = {
            { "Dead Ball (r=0.15)",  -16.0f, 0.15f, { 0.85f, 0.25f, 0.2f } },
            { "Bouncy Ball (r=0.55)",-14.0f, 0.55f, { 0.95f, 0.6f, 0.15f } },
            { "Super Ball (r=0.95)", -12.0f, 0.95f, { 0.3f, 0.9f, 0.3f } },
        };
        for (const auto& b : balls){
            auto e = m_Scene->CreateEntity(b.name);
            auto& p = e.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere);
            matte(p.material, b.color);
            p.material.emissive = b.color;
            p.material.emissiveStrength = 0.6f;
            auto& t = e.GetComponent<TransformComponent>();
            t.Translation = { b.x, 6.0f, 4.0f };
            t.Scale = glm::vec3(0.8f);
            auto& rb = e.AddComponent<RigidBodyComponent>();
            rb.restitution = b.restitution;
            rb.friction = 0.4f;
            e.AddComponent<SphereColliderComponent>();
        }
    }

    // ---- Friction ramp: ice vs rubber ----
    {
        auto ramp = m_Scene->CreateEntity("Ramp");
        auto& c = ramp.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.5f, 0.42f, 0.34f));
        auto& t = ramp.GetComponent<TransformComponent>();
        t.Translation = { -5.0f, 1.6f, -6.0f };
        t.Rotation = { 0.0f, 0.0f, glm::radians(-25.0f) };
        t.Scale = { 7.0f, 0.3f, 3.0f };
        auto& rb = ramp.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Static;
        rb.friction = 0.6f;
        ramp.AddComponent<BoxColliderComponent>();
    }
    {
        auto ice = m_Scene->CreateEntity("Ice Cube (friction 0.02)");
        auto& c = ice.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.55f, 0.8f, 1.0f));
        c.material.specular = glm::vec3(0.9f);
        c.material.shininess = 96.0f;
        auto& t = ice.GetComponent<TransformComponent>();
        t.Translation = { -7.4f, 3.15f, -6.7f };
        t.Rotation = { 0.0f, 0.0f, glm::radians(-25.0f) };
        t.Scale = glm::vec3(0.8f);
        auto& rb = ice.AddComponent<RigidBodyComponent>();
        rb.friction = 0.02f;
        ice.AddComponent<BoxColliderComponent>();
    }
    {
        auto rubber = m_Scene->CreateEntity("Rubber Cube (friction 1.2)");
        auto& c = rubber.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.25f, 0.2f, 0.2f));
        auto& t = rubber.GetComponent<TransformComponent>();
        t.Translation = { -7.4f, 3.15f, -5.3f };
        t.Rotation = { 0.0f, 0.0f, glm::radians(-25.0f) };
        t.Scale = glm::vec3(0.8f);
        auto& rb = rubber.AddComponent<RigidBodyComponent>();
        rb.friction = 1.2f;
        rubber.AddComponent<BoxColliderComponent>();
    }

    // ---- Wrecking ball: chain of point joints swings into a crate pyramid ----
    {
        // Visual anchor block (no physics; the top link is pinned to the world).
        auto anchor = m_Scene->CreateEntity("Chain Anchor");
        auto& c = anchor.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.2f, 0.2f, 0.24f));
        auto& t = anchor.GetComponent<TransformComponent>();
        t.Translation = { 8.0f, 7.2f, -4.0f };
        t.Scale = glm::vec3(0.5f);
    }
    for (int i = 0; i < 3; i++){
        auto link = m_Scene->CreateEntity("Chain Link " + std::to_string(i + 1));
        auto& p = link.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere);
        matte(p.material, glm::vec3(0.55f, 0.55f, 0.6f));
        auto& t = link.GetComponent<TransformComponent>();
        t.Translation = { 8.0f, 6.4f - 1.2f * i, -4.0f };
        t.Scale = glm::vec3(0.4f);
        auto& rb = link.AddComponent<RigidBodyComponent>();
        rb.density = 7000.0f;
        link.AddComponent<SphereColliderComponent>();
        auto& j = link.AddComponent<JointComponent>();
        j.type = JointComponent::Type::Point;
        j.connectedTag = (i == 0) ? "" : ("Chain Link " + std::to_string(i));
        j.pivot = { 8.0f, 7.0f - 1.2f * i, -4.0f };
    }
    {
        auto ball = m_Scene->CreateEntity("Wrecking Ball");
        auto& p = ball.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere);
        matte(p.material, glm::vec3(0.3f, 0.3f, 0.34f));
        p.material.specular = glm::vec3(0.8f);
        p.material.shininess = 64.0f;
        auto& t = ball.GetComponent<TransformComponent>();
        t.Translation = { 8.0f, 2.4f, -4.0f };
        t.Scale = glm::vec3(1.4f);
        auto& rb = ball.AddComponent<RigidBodyComponent>();
        rb.density = 3000.0f;
        rb.initialVelocity = { 0.0f, 0.0f, -9.0f }; // start the swing
        ball.AddComponent<SphereColliderComponent>();
        auto& j = ball.AddComponent<JointComponent>();
        j.type = JointComponent::Type::Point;
        j.connectedTag = "Chain Link 3";
        j.pivot = { 8.0f, 3.4f, -4.0f };
    }
    {
        // Crate pyramid in the swing path.
        const glm::vec3 crateColor(0.75f, 0.55f, 0.3f);
        int n = 0;
        for (int row = 0; row < 4; row++){
            int count = 4 - row;
            for (int i = 0; i < count; i++){
                auto crate = m_Scene->CreateEntity("Crate " + std::to_string(++n));
                auto& c = crate.AddComponent<CubeComponent>();
                matte(c.material, crateColor * (0.85f + 0.05f * row));
                auto& t = crate.GetComponent<TransformComponent>();
                t.Translation = { 8.0f - (count - 1) * 0.5f + i, 0.5f + row, -6.2f };
                auto& rb = crate.AddComponent<RigidBodyComponent>();
                rb.density = 400.0f;
                rb.friction = 0.6f;
                crate.AddComponent<BoxColliderComponent>();
            }
        }
    }

    // ---- Seesaw: limited hinge, heavy sphere launches the light cube ----
    {
        auto fulcrum = m_Scene->CreateEntity("Fulcrum");
        auto& c = fulcrum.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.3f, 0.3f, 0.35f));
        auto& t = fulcrum.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, 0.3f, 6.0f };
        t.Scale = { 0.4f, 0.6f, 1.2f };
        auto& rb = fulcrum.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Static;
        fulcrum.AddComponent<BoxColliderComponent>();
    }
    {
        auto plank = m_Scene->CreateEntity("Seesaw Plank");
        auto& c = plank.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.6f, 0.45f, 0.25f));
        auto& t = plank.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, 0.7f, 6.0f };
        t.Scale = { 6.0f, 0.15f, 1.2f };
        auto& rb = plank.AddComponent<RigidBodyComponent>();
        rb.density = 700.0f;
        rb.friction = 0.8f;
        plank.AddComponent<BoxColliderComponent>();
        auto& j = plank.AddComponent<JointComponent>();
        j.type = JointComponent::Type::Hinge;
        j.pivot = { 0.0f, 0.7f, 6.0f };
        j.axis = { 0.0f, 0.0f, 1.0f };
        j.limitAngles = true;
        j.minAngle = glm::radians(-25.0f);
        j.maxAngle = glm::radians(25.0f);
    }
    {
        auto rider = m_Scene->CreateEntity("Seesaw Rider");
        auto& c = rider.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.9f, 0.8f, 0.2f));
        auto& t = rider.GetComponent<TransformComponent>();
        t.Translation = { 2.4f, 1.2f, 6.0f };
        t.Scale = glm::vec3(0.8f);
        auto& rb = rider.AddComponent<RigidBodyComponent>();
        rb.density = 400.0f;
        rb.friction = 0.6f;
        rider.AddComponent<BoxColliderComponent>();
    }
    {
        auto weight = m_Scene->CreateEntity("Drop Weight");
        auto& p = weight.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere);
        matte(p.material, glm::vec3(0.4f, 0.4f, 0.45f));
        p.material.specular = glm::vec3(0.7f);
        auto& t = weight.GetComponent<TransformComponent>();
        t.Translation = { -2.4f, 7.0f, 6.0f };
        auto& rb = weight.AddComponent<RigidBodyComponent>();
        rb.density = 5000.0f;
        weight.AddComponent<SphereColliderComponent>();
    }

    // ---- Spinner: a kinematic paddle driven by Lua sweeps light crates ----
    {
        auto paddle = m_Scene->CreateEntity("Spinner Paddle");
        auto& c = paddle.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.7f, 0.3f, 0.6f));
        c.material.emissive = glm::vec3(0.7f, 0.3f, 0.6f);
        c.material.emissiveStrength = 0.8f;
        auto& t = paddle.GetComponent<TransformComponent>();
        t.Translation = { -8.0f, 0.5f, 12.0f };
        t.Scale = { 5.0f, 0.5f, 0.4f };
        auto& rb = paddle.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Kinematic;
        paddle.AddComponent<BoxColliderComponent>();
        paddle.AddComponent<LuaScriptComponent>().source =
            "-- Kinematic body: the entity transform drives the physics body.\n"
            "function OnUpdate(dt)\n"
            "    Rotate(0, 1.5 * dt, 0)\n"
            "end\n";
    }
    for (int i = 0; i < 6; i++){
        float a = glm::radians(60.0f * i);
        auto box = m_Scene->CreateEntity("Swept Box " + std::to_string(i + 1));
        auto& c = box.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.35f, 0.6f, 0.8f));
        auto& t = box.GetComponent<TransformComponent>();
        t.Translation = { -8.0f + 2.0f * std::cos(a), 0.25f, 12.0f + 2.0f * std::sin(a) };
        t.Scale = glm::vec3(0.5f);
        auto& rb = box.AddComponent<RigidBodyComponent>();
        rb.density = 300.0f;
        rb.friction = 0.3f;
        box.AddComponent<BoxColliderComponent>();
    }

    // ---- Balloon: negative gravity factor held down by a distance tether ----
    {
        auto anchor = m_Scene->CreateEntity("Tether Anchor");
        auto& c = anchor.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.25f, 0.25f, 0.3f));
        auto& t = anchor.GetComponent<TransformComponent>();
        t.Translation = { 6.0f, 0.15f, 10.0f };
        t.Scale = glm::vec3(0.3f);
    }
    {
        auto balloon = m_Scene->CreateEntity("Balloon");
        auto& p = balloon.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere);
        matte(p.material, glm::vec3(1.0f, 0.45f, 0.75f));
        p.material.emissive = glm::vec3(1.0f, 0.45f, 0.75f);
        p.material.emissiveStrength = 1.5f;
        auto& t = balloon.GetComponent<TransformComponent>();
        t.Translation = { 6.0f, 3.0f, 10.0f };
        t.Scale = glm::vec3(1.2f);
        auto& rb = balloon.AddComponent<RigidBodyComponent>();
        rb.gravityFactor = -0.3f;   // floats up
        rb.linearDamping = 0.4f;
        rb.initialVelocity = { 1.5f, 0.0f, 0.0f }; // sway on the tether
        balloon.AddComponent<SphereColliderComponent>();
        auto& j = balloon.AddComponent<JointComponent>();
        j.type = JointComponent::Type::Distance;
        j.pivot = { 6.0f, 0.15f, 10.0f };
        j.minDistance = 0.0f;   // rope: can slacken...
        j.maxDistance = -1.0f;  // ...but not stretch past the starting length
    }

    // ---- Cannon lane: Space fires a CCD ball through a sensor gate into crates ----
    {
        auto ballE = m_Scene->CreateEntity("Cannonball");
        auto& p = ballE.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere);
        matte(p.material, glm::vec3(0.9f, 0.35f, 0.1f));
        p.material.emissive = glm::vec3(0.9f, 0.35f, 0.1f);
        p.material.emissiveStrength = 1.2f;
        auto& t = ballE.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, 0.5f, 14.0f };
        t.Scale = glm::vec3(0.9f);
        auto& rb = ballE.AddComponent<RigidBodyComponent>();
        rb.density = 6000.0f;
        rb.continuousCollision = true; // fast mover: don't tunnel through the wall
        ballE.AddComponent<SphereColliderComponent>();
        ballE.AddComponent<LuaScriptComponent>().source =
            "-- Press SPACE to fire the cannonball at the crate wall.\n"
            "function OnUpdate(dt)\n"
            "    if IsKeyPressed(Key.Space) then\n"
            "        SetVelocity(0, 3, -26)\n"
            "    end\n"
            "end\n";
    }
    {
        auto gate = m_Scene->CreateEntity("Sensor Gate");
        auto& c = gate.AddComponent<CubeComponent>();
        matte(c.material, glm::vec3(0.1f, 0.6f, 0.8f));
        c.material.emissive = glm::vec3(0.0f, 0.8f, 1.0f);
        c.material.emissiveStrength = 1.0f;
        auto& t = gate.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, 1.6f, 8.0f };
        t.Scale = { 3.4f, 3.2f, 0.25f };
        auto& rb = gate.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Static;
        rb.isSensor = true; // trigger volume: detects, never blocks
        gate.AddComponent<BoxColliderComponent>();
        gate.AddComponent<LuaScriptComponent>().source =
            "-- Sensor gate: flashes green while something is inside it.\n"
            "function OnCollisionEnter(other)\n"
            "    SetEmissive(0.1, 1.0, 0.4, 6.0)\n"
            "end\n"
            "function OnCollisionExit(other)\n"
            "    SetEmissive(0.0, 0.8, 1.0, 1.0)\n"
            "end\n";
    }
    {
        // Crate wall target.
        for (int row = 0; row < 4; row++){
            for (int i = 0; i < 5; i++){
                auto crate = m_Scene->CreateEntity("Wall Crate " + std::to_string(row * 5 + i + 1));
                auto& c = crate.AddComponent<CubeComponent>();
                glm::vec3 color = ((row + i) % 2 == 0) ? glm::vec3(0.7f, 0.5f, 0.3f) : glm::vec3(0.55f, 0.4f, 0.25f);
                matte(c.material, color);
                auto& t = crate.GetComponent<TransformComponent>();
                t.Translation = { -1.84f + 0.92f * i, 0.45f + 0.92f * row, 2.0f };
                t.Scale = glm::vec3(0.9f);
                auto& rb = crate.AddComponent<RigidBodyComponent>();
                rb.density = 300.0f;
                rb.friction = 0.4f;
                crate.AddComponent<BoxColliderComponent>();
            }
        }
    }
}

void MainLayer::BuildMultiShadowShowcase() {
    // Three coloured directional lights from different angles, each casting its OWN
    // shadow map -> the central objects cast three shadows fanning out, tinted where the
    // coloured lights overlap. Toggle "Casts Shadow" / "Active" per light to compare.
    m_Scene->AmbientColor() = glm::vec3(0.05f);

    {
        auto cam = m_Scene->CreateEntity("Camera");
        cam.AddComponent<CameraComponent>();
        auto& t = cam.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, 7.0f, 13.0f };
        t.Rotation = { glm::radians(-22.0f), 0.0f, 0.0f };
    }

    // Three suns, 120 deg apart around Y, each tilted down and a different colour.
    struct Sun { const char* name; float yawDeg; glm::vec3 color; };
    const Sun suns[] = {
        { "Sun Red",   0.0f,   { 1.0f, 0.3f, 0.3f } },
        { "Sun Green", 120.0f, { 0.3f, 1.0f, 0.4f } },
        { "Sun Blue",  240.0f, { 0.4f, 0.5f, 1.0f } },
    };
    for (const auto& s : suns){
        auto sun = m_Scene->CreateDirLightEntity(s.name);
        auto& t = sun.GetComponent<TransformComponent>();
        t.Rotation = { glm::radians(-50.0f), glm::radians(s.yawDeg), 0.0f };
        auto& dl = sun.GetComponent<DirLightComponent>();
        dl.ambient  = glm::vec3(0.02f);
        dl.diffuse  = s.color;
        dl.specular = s.color;
        dl.castsShadow = true;
    }

    // Floor (receives the three shadows).
    {
        auto floor = m_Scene->CreateEntity("Floor");
        auto& c = floor.AddComponent<CubeComponent>();
        c.material.ambient = glm::vec3(0.6f);
        c.material.diffuse = glm::vec3(0.7f);
        c.material.specular = glm::vec3(0.1f);
        c.material.shininess = 8.0f;
        auto& t = floor.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, -1.0f, 0.0f };
        t.Scale = { 26.0f, 0.5f, 26.0f };
    }

    // Central caster + a couple of shapes, white-ish so the coloured lights tint them.
    auto makeShape = [&](const char* name, PrimitiveComponent::Type type, glm::vec3 pos, glm::vec3 scale){
        auto e = m_Scene->CreateEntity(name);
        auto& p = e.AddComponent<PrimitiveComponent>(type);
        p.material.ambient = glm::vec3(0.25f);
        p.material.diffuse = glm::vec3(0.85f);
        p.material.specular = glm::vec3(0.4f);
        p.material.shininess = 32.0f;
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = pos;
        t.Scale = scale;
    };
    makeShape("Pillar", PrimitiveComponent::Type::Cylinder, { 0.0f, 1.0f, 0.0f }, { 1.2f, 4.0f, 1.2f });
    makeShape("Sphere", PrimitiveComponent::Type::Sphere,   { 3.5f, 0.0f, 1.0f }, glm::vec3(1.5f));
    makeShape("Cone",   PrimitiveComponent::Type::Cone,     { -3.5f, 0.0f, -1.0f }, { 1.6f, 2.6f, 1.6f });
}

void MainLayer::SculptTerrain(TerrainComponent& terrain, const glm::vec3& terrainPos,
                              const glm::vec3& terrainScale, const glm::vec3& rayOrigin,
                              const glm::vec3& rayDir, float dt) {
    int N = terrain.resolution;
    float half = terrain.size * 0.5f;
    float cell = (N > 1) ? terrain.size / (float)(N - 1) : terrain.size;
    float sx = (terrainScale.x != 0.0f) ? terrainScale.x : 1.0f;
    float sz = (terrainScale.z != 0.0f) ? terrainScale.z : 1.0f;
    float sy = (terrainScale.y != 0.0f) ? terrainScale.y : 1.0f;

    // Bilinearly sample the heightfield at a local (x,z).
    auto sampleHeight = [&](float lx, float lz) -> float {
        float gx = (lx + half) / cell;
        float gz = (lz + half) / cell;
        int x0 = (int)std::floor(gx), z0 = (int)std::floor(gz);
        float fx = gx - x0, fz = gz - z0;
        auto H = [&](int x, int z) {
            x = std::max(0, std::min(N - 1, x));
            z = std::max(0, std::min(N - 1, z));
            return terrain.heights[(size_t)z * N + x];
        };
        float h0 = H(x0, z0) * (1 - fx) + H(x0 + 1, z0) * fx;
        float h1 = H(x0, z0 + 1) * (1 - fx) + H(x0 + 1, z0 + 1) * fx;
        return h0 * (1 - fz) + h1 * fz;
    };

    // March the ray and find where it crosses the actual terrain surface (so the
    // brush lands under the cursor even on already-sculpted hills/valleys).
    bool found = false;
    float localX = 0.0f, localZ = 0.0f;
    const float maxT = 500.0f;
    const float stepT = 0.15f;
    for (float t = 0.0f; t < maxT; t += stepT){
        glm::vec3 p = rayOrigin + rayDir * t;
        float lx = (p.x - terrainPos.x) / sx;
        float lz = (p.z - terrainPos.z) / sz;
        if (lx < -half || lx > half || lz < -half || lz > half) { continue; }
        float surfaceY = terrainPos.y + sampleHeight(lx, lz) * sy;
        if (p.y <= surfaceY){ localX = lx; localZ = lz; found = true; break; }
    }
    if (!found) { return; }

    int gx = (int)std::lround((localX + half) / cell);
    int gz = (int)std::lround((localZ + half) / cell);
    int rad = (int)std::ceil(m_BrushRadius / cell);
    float delta = m_BrushStrength * dt;

    bool changed = false;
    for (int dz = -rad; dz <= rad; dz++){
        for (int dx = -rad; dx <= rad; dx++){
            int x = gx + dx, z = gz + dz;
            if (x < 0 || x >= N || z < 0 || z >= N) { continue; }
            float wx = ((float)x * cell - half) - localX;
            float wz = ((float)z * cell - half) - localZ;
            float dist = std::sqrt(wx * wx + wz * wz);
            if (dist > m_BrushRadius) { continue; }
            float fall = 1.0f - dist / m_BrushRadius;
            float& h = terrain.heights[(size_t)z * N + x];
            if (m_BrushMode == 0) { h += delta * fall; }
            else if (m_BrushMode == 1) { h -= delta * fall; }
            else { // smooth toward the neighbour average
                int xm = std::max(0, x - 1), xp = std::min(N - 1, x + 1);
                int zm = std::max(0, z - 1), zp = std::min(N - 1, z + 1);
                float avg = (terrain.heights[(size_t)z * N + xm] + terrain.heights[(size_t)z * N + xp]
                           + terrain.heights[(size_t)zm * N + x] + terrain.heights[(size_t)zp * N + x]) * 0.25f;
                h += (avg - h) * fall * std::min(1.0f, dt * 8.0f);
            }
            changed = true;
        }
    }
    if (changed) { terrain.dirty = true; }
}

// True if this frame had a discrete input that could have edited the scene without
// leaving a widget active (a click on a menu item/checkbox, a shortcut key).
static bool HadDiscreteInput() {
    const ImGuiIO& io = ImGui::GetIO();
    for (bool released : io.MouseReleased)
        if (released) { return true; }
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++)
        if (ImGui::IsKeyPressed((ImGuiKey)key, false)) { return true; }
    return false;
}

void MainLayer::CommitHistory() {
    // Don't record during simulation (transforms change every frame) or while an
    // edit is in progress (a gizmo drag / held slider) — wait until it settles.
    bool editing = ImGuizmo::IsUsing() || ImGui::IsAnyItemActive() || m_Sculpting;
    bool editEnded = m_WasEditing && !editing;
    m_WasEditing = editing;
    if (m_Scene->IsRunning() || editing)
        return;

    // Serializing the whole scene (terrain heights, Lua sources, ...) is expensive,
    // so only snapshot on frames where an edit could have landed.
    if (!editEnded && !HadDiscreteInput())
        return;

    std::string current = SceneSerializer(m_Scene).SerializeToString();
    if (current == m_LastSnapshot)
        return;

    m_UndoStack.push_back(m_LastSnapshot);
    if (m_UndoStack.size() > 100)
        m_UndoStack.erase(m_UndoStack.begin());
    m_RedoStack.clear();
    m_LastSnapshot = std::move(current);
}

void MainLayer::RestoreScene(const std::string& snapshot) {
    // Reconcile the existing scene in place (keeps loaded models + selection)
    // instead of rebuilding it from scratch.
    SceneSerializer(m_Scene).ApplyFromString(snapshot);
    // Re-serialize the live scene as the baseline so CommitHistory doesn't record
    // a spurious entry (entity ids can differ from the snapshot after create/delete).
    m_LastSnapshot = SceneSerializer(m_Scene).SerializeToString();
}

bool MainLayer::MouseRay(const ImVec2& imageMin, glm::vec3& outOrigin, glm::vec3& outDir) const {
    ImVec2 mouse = ImGui::GetMousePos();
    float mx = mouse.x - imageMin.x;
    float my = mouse.y - imageMin.y;
    if (mx < 0.0f || my < 0.0f || mx >= m_ViewportSize.x || my >= m_ViewportSize.y)
        return false;

    float ndcX = (mx / m_ViewportSize.x) * 2.0f - 1.0f;
    float ndcY = 1.0f - (my / m_ViewportSize.y) * 2.0f;
    // Unproject with the camera the viewport was rendered with (a primary scene
    // camera may be active), so rays hit what is under the cursor.
    glm::mat4 invViewProj = glm::inverse(m_Scene->GetActiveProjection() * m_Scene->GetActiveView());
    glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint  = invViewProj * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;

    outOrigin = glm::vec3(nearPoint);
    outDir = glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));
    return true;
}

void MainLayer::BuildDefaultLayout(ImGuiID dockspaceID) {
    //  +------------+--------------------------------+---------------+
    //  | Hierarchy  |                                |  Properties   |
    //  |            |            Viewport            |               |
    //  +------------+                                +---------------+
    //  | Settings   +--------------------------------+ Agent Monitor |
    //  | Profiler.. | Training | Timeline | Script.. |               |
    //  +------------+--------------------------------+---------------+
    ImGui::DockBuilderRemoveNode(dockspaceID);
    ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockspaceID;
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.42f, nullptr, &right);
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.26f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.36f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    for (const char* name : { "Settings", "Profiler", "Shader Library" })
        ImGui::DockBuilderDockWindow(name, leftBottom);
    ImGui::DockBuilderDockWindow("Properties", right);
    ImGui::DockBuilderDockWindow("Agent Monitor", rightBottom);
    for (const char* name : { "Training", "Timeline", "Script Editor", "Textures" })
        ImGui::DockBuilderDockWindow(name, bottom);
    ImGui::DockBuilderDockWindow("Viewport", center);

    // The viewport never shares its node, so drop its tab strip.
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(center))
        node->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar;

    ImGui::DockBuilderFinish(dockspaceID);

    // New windows take focus as they first appear, which selects the last-created tab
    // in each group; pick the intended tabs once the windows exist (see OnImGuiRender).
    m_SelectDefaultTabsFrames = 2;
}

void MainLayer::SetActiveScene(const Ref<Scene>& scene) {
    // The old scene is being discarded: just tear its simulation down, no restore.
    m_Environment.reset();
    if (m_Scene && m_Scene->IsRunning())
        m_Scene->OnRuntimeStop();
    m_RuntimeSnapshot.clear();
    // A different environment: its learning curves and policy don't carry over.
    m_EpisodeLog.Clear();
    m_Paused = false;

    m_Scene = scene;
    m_Scene->OnResize(m_ViewportSize.x, m_ViewportSize.y);
    // Keep the editor (texture palette, panel visibility) and just retarget it.
    if (m_SceneEditor)
        m_SceneEditor->SetContext(m_Scene);
    else
        m_SceneEditor = CreateRef<SceneEditor>(m_Scene);

    // Snapshots of another scene can't be applied to this one.
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_LastSnapshot = SceneSerializer(m_Scene).SerializeToString();
}

void MainLayer::LoadShowcase(void (MainLayer::*build)(), const char* name) {
    SetActiveScene(CreateRef<Scene>());
    m_SceneName = name;
    (this->*build)(); // builders populate m_Scene
    m_LastSnapshot = SceneSerializer(m_Scene).SerializeToString();
}

void MainLayer::StartRuntime() {
    m_RuntimeSnapshot = SceneSerializer(m_Scene).SerializeToString();
    m_Scene->OnRuntimeStart();
    m_Paused = false;
    m_Environment = CreateScope<Environment>(m_Scene.get(), &m_EpisodeLog);
    m_Environment->Start();
}

void MainLayer::StopRuntime() {
    m_Environment.reset();
    // A connected trainer would otherwise restart Play with its next request.
    if (m_Bridge.HasClient()) { m_Bridge.Disconnect(); }
    m_Scene->OnRuntimeStop();
    // Undo everything the simulation and scripts changed (transforms, colours,
    // spawned/destroyed entities) so Stop returns to the edit-time scene.
    if (!m_RuntimeSnapshot.empty())
        RestoreScene(m_RuntimeSnapshot);
    m_RuntimeSnapshot.clear();
}

void MainLayer::Undo() {
    if (m_Scene->IsRunning() || m_UndoStack.empty())
        return;
    m_RedoStack.push_back(m_LastSnapshot);
    std::string snapshot = m_UndoStack.back();
    m_UndoStack.pop_back();
    RestoreScene(snapshot);
}

void MainLayer::Redo() {
    if (m_Scene->IsRunning() || m_RedoStack.empty())
        return;
    m_UndoStack.push_back(m_LastSnapshot);
    std::string snapshot = m_RedoStack.back();
    m_RedoStack.pop_back();
    RestoreScene(snapshot);
}

void MainLayer::OnDetach() {
    PHX_INFO("{0} detached.", this->layer_name);
}

void MainLayer::UpdateSimulation(float frameSeconds) {
    // Python bridge lifecycle follows the Training panel's checkbox.
    if (m_BridgeEnabled && !m_Bridge.IsListening() && m_BridgeFailedPort != m_BridgePort){
        if (!m_Bridge.Start(m_BridgePort)) { m_BridgeFailedPort = m_BridgePort; }
    }
    if (!m_BridgeEnabled){
        if (m_Bridge.IsListening()) { m_Bridge.Stop(); }
        m_BridgeFailedPort = 0;
    }

    if (!m_Environment){
        // A client asking for a reset while editing starts the simulation for it.
        if (m_BridgeEnabled){
            m_Bridge.Process(nullptr, 1.0f);
            if (m_Bridge.WantsPlay() && !m_Scene->IsRunning()) { StartRuntime(); }
        }
        return;
    }

    Environment& env = *m_Environment;
    env.timeScale = m_TimeScale;
    env.maxSpeed = m_MaxSpeed;
    env.paused = m_Paused;
    env.externalControl = m_BridgeEnabled;
    // Fast-forwarding without drawing the viewport leaves most of the frame to the simulation.
    env.frameBudgetMs = !m_MaxSpeed ? 14.0f : (m_RenderAtMaxSpeed ? 22.0f : 40.0f);
    if (m_BridgeEnabled) { m_Bridge.Process(&env, 20.0f); }
    {
        PHX_PROFILE("Simulation");
        env.Update(frameSeconds);
    }
}

void MainLayer::OnUpdate(Phoenix::Timestep ts) {
    UpdateSimulation((float)ts);
    // The viewport keeps its last image while fast-forwarding with rendering off.
    if (m_Environment && m_MaxSpeed && !m_RenderAtMaxSpeed)
        return;

    Phoenix::RenderCommand::SetClearColor(glm::vec4(0.0, 0.0, 0.0, 1.0));
    Phoenix::RenderCommand::Clear();

    FramebufferSpecification spec = m_Framebuffer->GetSpecification();
    if ( m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
        (spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y)){
        m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        m_Scene->OnResize(m_ViewportSize.x, m_ViewportSize.y);
        m_MainCamera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
    }

    // Update
    if (m_ViewportHovered){
        PHX_PROFILE("MainCamera Update");
        m_MainCamera.OnUpdate(ts);
    }

    // Linear lighting needs the HDR path even without bloom: its composite pass is
    // where the linear result is tonemapped and encoded to sRGB for display.
    const bool linear = Phoenix::Renderer::IsLinearLighting();
    if (m_BloomEnabled || linear){
        // Render the scene to the HDR target, then bloom + composite into m_Framebuffer.
        m_Bloom.Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        m_Bloom.BindHDRTarget();
        Phoenix::RenderCommand::SetClearColor(glm::vec4(Phoenix::Renderer::ToLinear(m_BackgroundColor), 1.0));
        Phoenix::RenderCommand::Clear();
        {
            PHX_PROFILE("Scene Update");
            m_Scene->OnRender(m_MainCamera, ts, m_SceneEditor->GetSelectedEntity());
        }
        m_Bloom.Composite(m_Framebuffer->GetRendererID(), m_BloomEnabled ? m_BloomIntensity : 0.0f,
                          m_BloomThreshold, m_Exposure, linear);
    }
    else{
        m_Framebuffer->Bind();
        Phoenix::RenderCommand::SetClearColor(glm::vec4(m_BackgroundColor, 1.0));
        Phoenix::RenderCommand::Clear();
        {
            PHX_PROFILE("Scene Update");
            m_Scene->OnRender(m_MainCamera, ts, m_SceneEditor->GetSelectedEntity());
        }
        m_Framebuffer->Unbind();
    }
}


void MainLayer::OnEvent(Phoenix::Event& e) {
    if (m_ViewportHovered){
        m_MainCamera.OnEvent(e);
    }
    
    EventDispatcher dispacher(e);
    dispacher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(MainLayer::OnResize));
}


namespace {
    // Window edges for the custom-frame resize (bitmask).
    enum WindowEdge { EdgeLeft = 1, EdgeRight = 2, EdgeTop = 4, EdgeBottom = 8 };

    constexpr float kTitleBarHeight = 38.0f;
    constexpr float kStatusBarHeight = 24.0f;

    // Background of the editor chrome (title/status bars, tab strips).
    ImVec4 ChromeColor() { return ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg); }
}

void MainLayer::NewScene() {
    SetActiveScene(CreateRef<Scene>());
    m_SceneName = "Untitled";
}

void MainLayer::ImportScene() {
    auto scene = CreateRef<Scene>();
    if (SceneSerializer(scene).Deserialize(m_ScenePath)) {
        SetActiveScene(scene);
        m_SceneName = m_ScenePath;
    }
}

void MainLayer::ExportScene() {
    SceneSerializer(m_Scene).Serialize(m_ScenePath);
    PHX_INFO("Scene exported to {0}", m_ScenePath);
}

void MainLayer::TogglePlay() {
    if (m_Scene->IsRunning()) { StopRuntime(); }
    else                      { StartRuntime(); }
}

void MainLayer::OnImGuiRender(){
    ImGuizmo::BeginFrame();
    HandleShortcuts();

    // Chrome first: the side bars shrink the viewport work area the dockspace fills.
    UpdateWindowResize();
    DrawTitleBar();
    DrawStatusBar();

    // Dockspace host covering the work area between the title and status bars.
    // Important: we proceed even if Begin() returns false (aka window is collapsed) to
    // keep the DockSpace() alive, or every docked window would lose its parent.
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ChromeColor());
    ImGui::Begin("##EditorDockHost", nullptr, hostFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    // DockSpace
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    float minWinSizeX = style.WindowMinSize.x;
    style.WindowMinSize.x = 260.0f;
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspace_id = ImGui::GetID("EditorDockSpace");
        // imgui.ini lives next to the executable, so a fresh build has no saved layout
        // and every panel would open floating on top of the others. Lay them out on the
        // first frame unless a saved layout already splits the dockspace.
        if (!m_LayoutChecked){
            m_LayoutChecked = true;
            ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
            if (!node || !node->IsSplitNode())
                m_ResetLayout = true;
        }
        if (m_ResetLayout){
            m_ResetLayout = false;
            BuildDefaultLayout(dockspace_id);
        }
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }
    style.WindowMinSize.x = minWinSizeX;

    DrawSettingsPanel();
    DrawProfilerPanel();
    DrawShaderLibraryPanel();
    {
        AIPanelContext ai = MakeAIContext();
        m_AIPanels.DrawTrainingPanel(ai);
        m_AIPanels.DrawAgentMonitor(ai);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
    ImGui::Begin("Viewport");

    m_ViewportFocused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();

    Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    // Top-left of the rendered image in screen space (used for the gizmo rect and picking).
    ImVec2 imageMin = ImGui::GetCursorScreenPos();
    uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
    ImGui::Image(reinterpret_cast<void*>(textureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

    // Transform gizmo for the selected entity. Skip it for the primary camera, since
    // we're looking THROUGH that camera and its gizmo would sit in the middle of the view.
    Entity selectedEntity = m_SceneEditor->GetSelectedEntity();
    bool selectedIsPrimaryCamera = selectedEntity && selectedEntity.HasComponent<CameraComponent>()
        && selectedEntity.GetComponent<CameraComponent>().primary;
    if (selectedEntity && m_GizmoType != -1 && !selectedIsPrimaryCamera && selectedEntity.HasComponent<TransformComponent>())
    {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imageMin.x, imageMin.y, m_ViewportSize.x, m_ViewportSize.y);

        // The camera the viewport was rendered with (a primary scene camera may be active).
        const glm::mat4& cameraProjection = m_Scene->GetActiveProjection();
        glm::mat4 cameraView = m_Scene->GetActiveView();

        auto& tc = selectedEntity.GetComponent<TransformComponent>();
        glm::mat4 transform = tc.GetTransform();

        // Snap to 0.5 units / 15 degrees / 0.1 scale steps; Ctrl inverts the toggle.
        bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
        bool snap = m_GizmoSnap != control;
        float snapValue = m_GizmoType == 1 ? 15.0f : (m_GizmoType == 2 ? 0.1f : 0.5f);
        float snapValues[3] = { snapValue, snapValue, snapValue };

        ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
            (ImGuizmo::OPERATION)m_GizmoType, m_GizmoWorld ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
            glm::value_ptr(transform), nullptr, snap ? snapValues : nullptr);

        if (ImGuizmo::IsUsing())
        {
            glm::vec3 translation, rotation, scale;
            if (Math::DecomposeTransform(transform, translation, rotation, scale))
            {
                glm::vec3 deltaRotation = rotation - tc.Rotation;
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;
            }
        }
    }

    // Sensors, reward zones and spawn ranges, then the tool strip on top (before picking,
    // so clicks on the tool buttons don't also select whatever is behind them).
    {
        AIPanelContext ai = MakeAIContext();
        m_AIPanels.DrawViewportOverlay(ai, imageMin, viewportPanelSize,
            m_Scene->GetActiveProjection() * m_Scene->GetActiveView());
    }
    DrawViewportOverlay(imageMin, viewportPanelSize);
    bool overlayHovered = ImGui::IsAnyItemHovered();

    // Terrain sculpting: while the tool is on and a Terrain is selected, LMB-drag in
    // the viewport raises/lowers/smooths the heightfield under the cursor.
    m_Sculpting = false;
    if (m_TerrainSculpt && selectedEntity && selectedEntity.HasComponent<TerrainComponent>()
        && m_ViewportHovered && !overlayHovered && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()
        && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        glm::vec3 rayOrigin, rayDir;
        if (MouseRay(imageMin, rayOrigin, rayDir))
        {
            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            SculptTerrain(selectedEntity.GetComponent<TerrainComponent>(), tc.Translation, tc.Scale,
                rayOrigin, rayDir, ImGui::GetIO().DeltaTime);
            m_Sculpting = true;
        }
    }

    // Click-to-select: cast a ray from the cursor and pick the nearest entity.
    // Skipped while interacting with the gizmo, orbiting, or sculpting.
    if (!m_TerrainSculpt && m_ViewportHovered && !overlayHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
    {
        glm::vec3 rayOrigin, rayDir;
        if (MouseRay(imageMin, rayOrigin, rayDir))
        {
            m_SceneEditor->SetSelectedEntity(m_Scene->PickEntity(rayOrigin, rayDir));
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();

    m_SceneEditor->OnImGuiRender();

    DrawHelpPopups();

    ImGui::End();

    // After a layout rebuild: show the hierarchy and the timeline in their tab groups
    // and leave keyboard focus on the viewport. Repeated for a second frame because the
    // dock tab bars only exist once the windows have been docked for a frame.
    if (m_SelectDefaultTabsFrames > 0){
        m_SelectDefaultTabsFrames--;
        ImGui::SetWindowFocus("Settings");
        ImGui::SetWindowFocus("Training");
        ImGui::SetWindowFocus("Viewport");
    }

    // Resize cursor over the undecorated window's edges (set last so it wins).
    int edges = m_ResizeEdges ? m_ResizeEdges : m_HoverEdges;
    if (edges){
        bool horizontal = edges & (EdgeLeft | EdgeRight), vertical = edges & (EdgeTop | EdgeBottom);
        if (horizontal && vertical){
            bool nwse = ((edges & EdgeLeft) && (edges & EdgeTop)) || ((edges & EdgeRight) && (edges & EdgeBottom));
            ImGui::SetMouseCursor(nwse ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
        }
        else{
            ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        }
    }

    // Record an undo snapshot once the current edit has settled.
    CommitHistory();
}

void MainLayer::UpdateWindowResize(){
    Window& window = Application::Get().GetWindow();
    m_HoverEdges = 0;
    if (!window.HasCustomTitleBar() || window.IsMaximized()){
        m_ResizeEdges = 0;
        return;
    }

    if (!m_ResizeEdges){
        // Hover test on the frame's mouse position; only start on a fresh click so a drag
        // that began elsewhere (a splitter, a slider) never turns into a window resize.
        const ImGuiIO& io = ImGui::GetIO();
        const float border = 5.0f;
        ImVec2 m = io.MousePos;
        bool inside = m.x >= 0.0f && m.y >= 0.0f && m.x < io.DisplaySize.x && m.y < io.DisplaySize.y;
        bool free = !ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (inside && free && !m_TitleDragging && !ImGui::IsAnyItemActive()){
            if (m.x < border)                       m_HoverEdges |= EdgeLeft;
            if (m.x >= io.DisplaySize.x - border)   m_HoverEdges |= EdgeRight;
            if (m.y < border)                       m_HoverEdges |= EdgeTop;
            if (m.y >= io.DisplaySize.y - border)   m_HoverEdges |= EdgeBottom;
        }
        if (m_HoverEdges && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
            m_ResizeEdges = m_HoverEdges;
            m_ResizeStartCursor = window.GetCursorScreenPosition();
            m_ResizeStartPos = window.GetPosition();
            m_ResizeStartSize = { (int)window.GetWidth(), (int)window.GetHeight() };
        }
        return;
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)){
        m_ResizeEdges = 0;
        return;
    }
    const int minWidth = 640, minHeight = 360;
    glm::vec2 delta = window.GetCursorScreenPosition() - m_ResizeStartCursor;
    glm::ivec2 pos = m_ResizeStartPos, size = m_ResizeStartSize;
    if (m_ResizeEdges & EdgeRight)  { size.x = std::max(minWidth, m_ResizeStartSize.x + (int)delta.x); }
    if (m_ResizeEdges & EdgeBottom) { size.y = std::max(minHeight, m_ResizeStartSize.y + (int)delta.y); }
    if (m_ResizeEdges & EdgeLeft){
        size.x = std::max(minWidth, m_ResizeStartSize.x - (int)delta.x);
        pos.x = m_ResizeStartPos.x + (m_ResizeStartSize.x - size.x);
    }
    if (m_ResizeEdges & EdgeTop){
        size.y = std::max(minHeight, m_ResizeStartSize.y - (int)delta.y);
        pos.y = m_ResizeStartPos.y + (m_ResizeStartSize.y - size.y);
    }
    if (pos != window.GetPosition())
        window.SetPosition(pos.x, pos.y);
    if (size.x != (int)window.GetWidth() || size.y != (int)window.GetHeight())
        window.SetSize(size.x, size.y);
}

void MainLayer::DrawTitleBar(){
    Window& window = Application::Get().GetWindow();
    const bool customFrame = window.HasCustomTitleBar();
    const bool running = m_Scene->IsRunning();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Logo and app name sit left of the menus; BeginMenuBar starts at MenuBarOffset, and
    // the y offset centres the menu items vertically in the taller bar.
    ImFont* headingFont = ImGuiLayer::GetFont(ImGuiLayer::FontType::Heading);
    const char* appName = "Phoenix";
    const float logoSize = 20.0f, leftPad = 14.0f;
    float nameWidth = headingFont->CalcTextSizeA(headingFont->FontSize, FLT_MAX, 0.0f, appName).x;
    float menuStartX = leftPad + logoSize + 8.0f + nameWidth + 18.0f;
    g.NextWindowData.MenuBarOffsetMinVal = ImVec2(menuStartX, IM_FLOOR((kTitleBarHeight - ImGui::GetFrameHeight()) * 0.5f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ChromeColor());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    bool open = ImGui::BeginViewportSideBar("##TitleBar", viewport, ImGuiDir_Up, kTitleBarHeight, flags);
    g.NextWindowData.MenuBarOffsetMinVal = ImVec2(0.0f, 0.0f);
    ImGui::PopStyleVar(2);
    if (!open){
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    ImGuiWindow* bar = ImGui::GetCurrentWindow();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 barMin = bar->Pos;
    const ImVec2 barMax(bar->Pos.x + bar->Size.x, bar->Pos.y + bar->Size.y);
    const float midY = (barMin.y + barMax.y) * 0.5f;

    if (ImGui::BeginMenuBar()){
        // Let everything below use the full bar height, not just the menu strip.
        ImGui::PushClipRect(barMin, barMax, false);

        // Logo + name.
        EditorUI::DrawIcon(dl, EditorUI::Icon::Logo, ImVec2(barMin.x + leftPad + logoSize * 0.5f, midY), logoSize, 0);
        dl->AddText(headingFont, headingFont->FontSize,
            ImVec2(barMin.x + leftPad + logoSize + 8.0f, midY - headingFont->FontSize * 0.5f), EditorUI::TextColor(), appName);

        DrawMenus();
        float menusEndX = ImGui::GetCursorScreenPos().x;

        // Window controls (undecorated window only), right to left.
        const float controlW = 46.0f;
        float right = barMax.x;
        if (customFrame){
            struct Control { const char* id; EditorUI::Icon icon; bool danger; };
            const Control controls[] = {
                { "##close", EditorUI::Icon::Close, true },
                { "##maximize", window.IsMaximized() ? EditorUI::Icon::Restore : EditorUI::Icon::Maximize, false },
                { "##minimize", EditorUI::Icon::Minimize, false },
            };
            for (const Control& c : controls){
                right -= controlW;
                ImGui::SetCursorScreenPos(ImVec2(right, barMin.y));
                bool pressed = ImGui::InvisibleButton(c.id, ImVec2(controlW, kTitleBarHeight));
                bool hovered = ImGui::IsItemHovered();
                if (hovered || ImGui::IsItemActive()){
                    ImU32 bg = c.danger ? EditorUI::Danger(ImGui::IsItemActive() ? 0.8f : 1.0f)
                                        : ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
                    dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), bg);
                }
                ImU32 iconCol = (hovered && c.danger) ? IM_COL32_WHITE : EditorUI::TextColor(hovered ? 1.0f : 0.8f);
                EditorUI::DrawIcon(dl, c.icon, ImVec2(right + controlW * 0.5f, midY), 20.0f, iconCol);
                if (pressed){
                    if (c.icon == EditorUI::Icon::Close)         { Application::Get().Close(); }
                    else if (c.icon == EditorUI::Icon::Minimize) { window.Minimize(); }
                    else if (window.IsMaximized())               { window.Restore(); }
                    else                                         { window.Maximize(); }
                }
            }
            right -= 8.0f;
        }
        else{
            right -= 12.0f;
        }

        // Scene name, right-aligned before the window controls.
        {
            ImFont* small = ImGuiLayer::GetFont(ImGuiLayer::FontType::Small);
            std::string label = m_SceneName;
            ImVec2 ts = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, label.c_str());
            float x = right - ts.x;
            if (x > menusEndX + 140.0f){
                dl->AddText(small, small->FontSize, ImVec2(x, midY - ts.y * 0.5f), EditorUI::MutedColor(), label.c_str());
                right = x - 12.0f;
            }
        }

        // Play / Stop, centred in the bar (pushed right if the menus would overlap it).
        {
            const char* label = running ? "Stop" : "Play";
            ImVec2 ts = ImGui::CalcTextSize(label);
            ImVec2 size(ts.x + 44.0f, 26.0f);
            float x = std::max(IM_FLOOR((barMin.x + barMax.x - size.x) * 0.5f), menusEndX + 16.0f);
            ImGui::SetCursorScreenPos(ImVec2(x, IM_FLOOR(midY - size.y * 0.5f)));
            bool pressed = ImGui::InvisibleButton("##PlayStop", size);
            bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            ImU32 bg = running ? EditorUI::Danger(held ? 0.45f : (hovered ? 0.35f : 0.22f))
                               : ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
            dl->AddRectFilled(mn, mx, bg, size.y * 0.5f);
            ImU32 iconCol = running ? EditorUI::Danger() : EditorUI::Success();
            EditorUI::DrawIcon(dl, running ? EditorUI::Icon::Stop : EditorUI::Icon::Play,
                ImVec2(mn.x + 18.0f, midY), 11.0f, iconCol);
            dl->AddText(ImVec2(mn.x + 30.0f, midY - ts.y * 0.5f), EditorUI::TextColor(), label);
            if (hovered)
                ImGui::SetTooltip(running ? "Stop and restore the edit-time scene (F5)" : "Run physics, scripts and agents (F5)");
            if (pressed)
                TogglePlay();

            // Pause / step while running, plus the current speed.
            if (running){
                float bx = mx.x + 6.0f;
                ImGui::SetCursorScreenPos(ImVec2(bx, mn.y));
                if (EditorUI::IconButton("##TitlePause", m_Paused ? EditorUI::Icon::Play : EditorUI::Icon::Pause,
                        ImVec2(size.y, size.y), m_Paused, m_Paused ? "Resume (F6)" : "Pause (F6)", 0.42f))
                    m_Paused = !m_Paused;
                ImGui::SetCursorScreenPos(ImVec2(bx + size.y + 2.0f, mn.y));
                if (EditorUI::IconButton("##TitleStep", EditorUI::Icon::StepForward, ImVec2(size.y, size.y), false,
                        "Step one decision (F7)", 0.42f) && m_Environment){
                    m_Paused = true;
                    m_Environment->paused = true;
                    m_Environment->StepDecision();
                }
                char speed[16];
                if (m_MaxSpeed) { std::snprintf(speed, sizeof(speed), "Max"); }
                else            { std::snprintf(speed, sizeof(speed), "%gx", m_TimeScale); }
                ImFont* small = ImGuiLayer::GetFont(ImGuiLayer::FontType::Small);
                dl->AddText(small, small->FontSize, ImVec2(bx + size.y * 2.0f + 10.0f, midY - small->FontSize * 0.5f),
                    EditorUI::MutedColor(), speed);
            }
        }

        ImGui::PopClipRect();
        ImGui::EndMenuBar();
    }

    // Bottom divider; turns accent-coloured while the simulation runs.
    dl->PushClipRect(barMin, barMax, false);
    dl->AddLine(ImVec2(barMin.x, barMax.y - 1.0f), ImVec2(barMax.x, barMax.y - 1.0f),
        running ? EditorUI::Accent() : ImGui::GetColorU32(ImGuiCol_Border), running ? 2.0f : 1.0f);
    dl->PopClipRect();

    // Drag the OS window by any empty part of the bar; double-click toggles maximize.
    if (customFrame){
        bool barHovered = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && !m_HoverEdges && !m_ResizeEdges;
        if (barHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){
            m_TitleDragging = false;
            if (window.IsMaximized()) { window.Restore(); }
            else                      { window.Maximize(); }
        }
        else if (barHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
            glm::vec2 cursor = window.GetCursorScreenPosition();
            glm::ivec2 pos = window.GetPosition();
            m_TitleDragging = true;
            m_TitleDragMoved = false;
            m_DragStartCursor = cursor;
            // Keep the grab point at the same fraction of the width, so dragging a
            // maximized window off restores it under the cursor.
            m_DragGrab = { (cursor.x - (float)pos.x) / std::max(1.0f, (float)window.GetWidth()), cursor.y - (float)pos.y };
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();

    if (m_TitleDragging){
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            m_TitleDragging = false;
        }
        else{
            glm::vec2 cursor = window.GetCursorScreenPosition();
            if (!m_TitleDragMoved && glm::length(cursor - m_DragStartCursor) > 4.0f){
                m_TitleDragMoved = true;
                if (window.IsMaximized())
                    window.Restore();
            }
            if (m_TitleDragMoved && !window.IsMaximized()){
                float grabX = m_DragGrab.x * (float)window.GetWidth();
                window.SetPosition((int)(cursor.x - grabX), (int)(cursor.y - m_DragGrab.y));
            }
        }
    }
}

void MainLayer::DrawMenus(){
    const bool running = m_Scene->IsRunning();
    Entity selected = m_SceneEditor->GetSelectedEntity();

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Scene", "Ctrl+N")) { NewScene(); }
        if (ImGui::BeginMenu("Showcases"))
        {
            ImGui::TextDisabled("Reinforcement learning");
            if (ImGui::MenuItem("AI: Find the Goal"))   { LoadShowcase(&MainLayer::BuildAIGoalShowcase, "AI: Find the Goal"); }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Four walker agents with ray sensors learn to reach a randomized goal."); }
            if (ImGui::MenuItem("AI: Roller Ball"))     { LoadShowcase(&MainLayer::BuildAIRollerShowcase, "AI: Roller Ball"); }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Balls learn continuous force control to reach a target without falling."); }
            ImGui::Separator();
            ImGui::TextDisabled("Engine");
            if (ImGui::MenuItem("Bloom & Shadows"))  { LoadShowcase(&MainLayer::BuildShowcaseScene, "Showcase"); }
            if (ImGui::MenuItem("Water"))            { LoadShowcase(&MainLayer::BuildWaterShowcase, "Water Showcase"); }
            if (ImGui::MenuItem("Robot"))            { LoadShowcase(&MainLayer::BuildRobotShowcase, "Robot Showcase"); }
            if (ImGui::MenuItem("Multi-Shadow"))     { LoadShowcase(&MainLayer::BuildMultiShadowShowcase, "Multi-Shadow Showcase"); }
            if (ImGui::MenuItem("Physics"))          { LoadShowcase(&MainLayer::BuildPhysicsShowcase, "Physics Showcase"); }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import Scene", "Ctrl+O")) { ImportScene(); }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Load %s from the executable's directory", m_ScenePath.c_str()); }
        if (ImGui::MenuItem("Export Scene", "Ctrl+S")) { ExportScene(); }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Save to %s next to the executable", m_ScenePath.c_str()); }
        ImGui::Separator();
        if (ImGui::MenuItem("Import Shader...")) { m_OpenImportShader = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) { Application::Get().Close(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !running && !m_UndoStack.empty())) { Undo(); }
        if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !running && !m_RedoStack.empty())) { Redo(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Deselect", "Esc", false, (bool)selected)) { m_SceneEditor->SetSelectedEntity({}); }
        if (ImGui::MenuItem("Delete Entity", "Del", false, (bool)selected)){
            m_Scene->DestroyEntity(selected);
            m_SceneEditor->SetSelectedEntity({});
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools"))
    {
        if (ImGui::MenuItem("Select", "Q", m_GizmoType == -1)) { m_GizmoType = -1; }
        if (ImGui::MenuItem("Move", "W", m_GizmoType == 0))    { m_GizmoType = 0; }
        if (ImGui::MenuItem("Rotate", "E", m_GizmoType == 1))  { m_GizmoType = 1; }
        if (ImGui::MenuItem("Scale", "R", m_GizmoType == 2))   { m_GizmoType = 2; }
        ImGui::Separator();
        ImGui::MenuItem("World Space Gizmo", nullptr, &m_GizmoWorld);
        ImGui::MenuItem("Snap to Increments", nullptr, &m_GizmoSnap);
        ImGui::Separator();
        ImGui::MenuItem("Terrain Sculpt", nullptr, &m_TerrainSculpt);
        ImGui::Separator();
        if (ImGui::MenuItem(running ? "Stop" : "Play", "F5")) { TogglePlay(); }
        if (ImGui::MenuItem(m_Paused ? "Resume" : "Pause", "F6", false, running)) { m_Paused = !m_Paused; }
        if (ImGui::MenuItem("Step Decision", "F7", false, running && m_Environment)){
            m_Paused = true;
            m_Environment->paused = true;
            m_Environment->StepDecision();
        }
        if (ImGui::MenuItem("Reset Episode", nullptr, false, running && m_Environment)) { m_Environment->ResetEpisode(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Training", nullptr, &m_AIPanels.showTraining);
        ImGui::MenuItem("Agent Monitor", nullptr, &m_AIPanels.showAgentMonitor);
        ImGui::MenuItem("AI Overlays", nullptr, &m_AIPanels.showOverlays);
        ImGui::Separator();
        ImGui::MenuItem("Settings", nullptr, &m_ShowSettings);
        ImGui::MenuItem("Profiler", nullptr, &m_ShowProfiler);
        ImGui::MenuItem("Shader Library", nullptr, &m_ShowShaderLibrary);
        if (m_SceneEditor){
            ImGui::MenuItem("Timeline", nullptr, &m_SceneEditor->ShowTimeline());
            ImGui::MenuItem("Script Editor", nullptr, &m_SceneEditor->ShowScriptEditor());
            ImGui::MenuItem("Textures", nullptr, &m_SceneEditor->ShowTextures());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("VSync", nullptr, &vsync)) { Application::Get().GetWindow().SetVSync(vsync); }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")){
            m_ResetLayout = true; // applied next frame, before the dockspace is submitted
            m_ShowSettings = m_ShowProfiler = m_ShowShaderLibrary = true;
            m_AIPanels.showTraining = m_AIPanels.showAgentMonitor = true;
            if (m_SceneEditor){
                m_SceneEditor->ShowTimeline() = true;
                m_SceneEditor->ShowScriptEditor() = true;
                m_SceneEditor->ShowTextures() = true;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("AI Sandbox Guide"))        { m_OpenGuide = true; }
        if (ImGui::MenuItem("Keyboard Shortcuts", "F1")) { m_OpenShortcuts = true; }
        if (ImGui::MenuItem("About Phoenix"))           { m_OpenAbout = true; }
        ImGui::EndMenu();
    }
}

void MainLayer::DrawStatusBar(){
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ChromeColor());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 0.0f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
    bool open = ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, kStatusBarHeight, flags);
    ImGui::PopStyleVar(2);
    if (open){
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddLine(wp, ImVec2(wp.x + ws.x, wp.y), ImGui::GetColorU32(ImGuiCol_Border));

        ImGui::PushFont(ImGuiLayer::GetFont(ImGuiLayer::FontType::Small));
        const ImGuiIO& io = ImGui::GetIO();
        const float textY = IM_FLOOR((kStatusBarHeight - ImGui::GetFontSize()) * 0.5f);
        const bool running = m_Scene->IsRunning();

        // Left: mode, entity count, selection.
        ImGui::SetCursorPos(ImVec2(10.0f, textY));
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(p.x + 4.0f, p.y + ImGui::GetFontSize() * 0.5f), 3.5f,
                running ? EditorUI::Danger() : EditorUI::Success());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 13.0f);
            ImGui::TextUnformatted(running ? "Playing" : "Editing");
        }
        auto separator = [](){
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0.0f, 10.0f);
        };
        separator();
        ImGui::Text("%d entities", m_SceneEditor->GetEntityCount());
        Entity selected = m_SceneEditor->GetSelectedEntity();
        if (selected && selected.HasComponent<TagComponent>()){
            separator();
            ImGui::TextDisabled("Selected");
            ImGui::SameLine();
            ImGui::TextUnformatted(selected.GetComponent<TagComponent>().Tag.c_str());
        }
        if (m_TerrainSculpt){
            separator();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(EditorUI::Accent()), "Sculpt mode");
        }
        if (m_Environment && !m_Environment->Agents().empty()){
            separator();
            ImGui::Text("Episode %d", m_Environment->Episode());
            ImGui::SameLine();
            ImGui::TextDisabled("step %d", m_Environment->EpisodeStep());
        }
        if (m_Bridge.HasClient()){
            separator();
            ImGui::TextColored(ImVec4(0.35f, 0.67f, 1.0f, 1.0f), "Python connected");
        }

        // Right: performance + history.
        char stats[128];
        if (m_Environment)
            std::snprintf(stats, sizeof(stats), "%.0f steps/s   |   %.1fx   |   %.0f FPS  (%.2f ms)",
                m_Environment->StepsPerSecond(), m_Environment->SimSpeed(),
                io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        else
            std::snprintf(stats, sizeof(stats), "Undo %d   |   %s   |   %.0f FPS  (%.2f ms)",
                (int)m_UndoStack.size(), vsync ? "VSync on" : "VSync off",
                io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        float w = ImGui::CalcTextSize(stats).x;
        ImGui::SameLine();
        float x = ImGui::GetWindowWidth() - w - 12.0f;
        if (x > ImGui::GetCursorPosX() + 20.0f){
            ImGui::SetCursorPos(ImVec2(x, textY));
            ImGui::TextDisabled("%s", stats);
        }
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void MainLayer::DrawViewportOverlay(const ImVec2& imageMin, const ImVec2& imageSize){
    if (imageSize.x < 120.0f || imageSize.y < 80.0f)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool running = m_Scene->IsRunning();
    const float margin = 10.0f, button = 28.0f, gap = 2.0f, inner = 4.0f;

    // Tool strip (top-left): gizmo operation, space and snapping.
    struct Tool { const char* id; EditorUI::Icon icon; int type; const char* tooltip; };
    const Tool tools[] = {
        { "##ToolSelect", EditorUI::Icon::Select, -1, "Select (Q)" },
        { "##ToolMove",   EditorUI::Icon::Move,    0, "Move (W)" },
        { "##ToolRotate", EditorUI::Icon::Rotate,  1, "Rotate (E)" },
        { "##ToolScale",  EditorUI::Icon::Scale,   2, "Scale (R)" },
    };
    const int toolCount = IM_ARRAYSIZE(tools);
    const float dividerW = 9.0f;
    ImVec2 stripMin(imageMin.x + margin, imageMin.y + margin);
    ImVec2 stripSize(inner * 2.0f + (toolCount + 3) * button + (toolCount + 2) * gap + dividerW, button + inner * 2.0f);
    dl->AddRectFilled(stripMin, ImVec2(stripMin.x + stripSize.x, stripMin.y + stripSize.y), IM_COL32(18, 19, 22, 215), 7.0f);
    dl->AddRect(stripMin, ImVec2(stripMin.x + stripSize.x, stripMin.y + stripSize.y), IM_COL32(255, 255, 255, 18), 7.0f);

    float x = stripMin.x + inner;
    const float y = stripMin.y + inner;
    for (const Tool& t : tools){
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (EditorUI::IconButton(t.id, t.icon, ImVec2(button, button), m_GizmoType == t.type, t.tooltip))
            m_GizmoType = t.type;
        x += button + gap;
    }
    dl->AddLine(ImVec2(x + dividerW * 0.5f - gap * 0.5f, y + 5.0f), ImVec2(x + dividerW * 0.5f - gap * 0.5f, y + button - 5.0f),
        IM_COL32(255, 255, 255, 40));
    x += dividerW;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (EditorUI::IconButton("##ToolSpace", m_GizmoWorld ? EditorUI::Icon::World : EditorUI::Icon::Local,
            ImVec2(button, button), false, m_GizmoWorld ? "World space handles (click for local)" : "Local space handles (click for world)"))
        m_GizmoWorld = !m_GizmoWorld;
    x += button + gap;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (EditorUI::IconButton("##ToolSnap", EditorUI::Icon::Magnet, ImVec2(button, button), m_GizmoSnap,
            "Snap: 0.5 units / 15 deg / 0.1 scale (hold Ctrl to invert)"))
        m_GizmoSnap = !m_GizmoSnap;
    x += button + gap;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (EditorUI::IconButton("##ToolAIOverlay", EditorUI::Icon::Eye, ImVec2(button, button), m_AIPanels.showOverlays,
            "AI overlays: ray sensors, reward zones, spawn ranges"))
        m_AIPanels.showOverlays = !m_AIPanels.showOverlays;

    // Play state: accent frame + badge (top-right).
    if (running){
        dl->AddRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), EditorUI::Danger(0.85f), 0.0f, 0, 2.0f);
        const char* text = m_Paused ? "Paused  -  F6 to resume, F7 to step"
                         : (m_MaxSpeed && !m_RenderAtMaxSpeed) ? "Training at max speed  -  view paused"
                         : "Playing  -  F5 to stop";
        ImVec2 size = EditorUI::PillSize(text, 5.0f);
        EditorUI::DrawPill(dl, ImVec2(imageMin.x + imageSize.x - size.x - margin, imageMin.y + margin + inner),
            text, EditorUI::Danger(), IM_COL32(18, 19, 22, 215), 5.0f);
    }

    // Sculpt tool hint (bottom-centre).
    if (m_TerrainSculpt){
        const char* modes[] = { "Raise", "Lower", "Smooth" };
        char text[128];
        std::snprintf(text, sizeof(text), "Sculpt: %s   radius %.1f   strength %.1f   -  drag on a selected terrain",
            modes[m_BrushMode], m_BrushRadius, m_BrushStrength);
        ImVec2 size = EditorUI::PillSize(text, 5.0f);
        EditorUI::DrawPill(dl, ImVec2(imageMin.x + (imageSize.x - size.x) * 0.5f, imageMin.y + imageSize.y - size.y - margin),
            text, EditorUI::Accent(), IM_COL32(18, 19, 22, 215), 5.0f);
    }
}

void MainLayer::DrawSettingsPanel(){
    if (!m_ShowSettings)
        return;
    ImGui::Begin("Settings", &m_ShowSettings);
    // Widgets take the space left of a fixed label column, so labels never clip.
    ImGui::PushItemWidth(-130.0f);

    if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)){
        SimulationSettings& sim = m_Scene->Simulation();
        int hz = (int)std::lround(1.0f / std::max(1e-4f, sim.fixedTimestep));
        ImGui::BeginDisabled(m_Scene->IsRunning());
        if (ImGui::SliderInt("Physics Rate", &hz, 20, 240, "%d Hz"))
            sim.fixedTimestep = 1.0f / (float)std::max(1, hz);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Fixed physics step while playing. Agents decide every Decision Period steps.");
        ImGui::InputInt("Random Seed", &sim.seed);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Seeds episode randomization (spawn randomizers, random policies). 0 = different every run.");
        ImGui::EndDisabled();
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::ColorEdit3("Background", glm::value_ptr(m_BackgroundColor));
        ImGui::ColorEdit3("Ambient Light", glm::value_ptr(m_Scene->AmbientColor()));
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)){
        bool linearLighting = Phoenix::Renderer::IsLinearLighting();
        if (ImGui::Checkbox("Linear Lighting (sRGB)", &linearLighting))
            Phoenix::Renderer::SetLinearLighting(linearLighting);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light in linear space: sRGB textures/colours are decoded and the\n"
                              "output is encoded for display. Physically correct falloff and\n"
                              "blending, but scenes tuned without it will look brighter.");
        ImGui::Checkbox("Bloom", &m_BloomEnabled);
        if (m_BloomEnabled || linearLighting)
            ImGui::DragFloat("Exposure", &m_Exposure, 0.05f, 0.1f, 8.0f);
        if (m_BloomEnabled){
            ImGui::DragFloat("Bloom Intensity", &m_BloomIntensity, 0.05f, 0.0f, 5.0f);
            ImGui::DragFloat("Bloom Threshold", &m_BloomThreshold, 0.05f, 0.0f, 5.0f);
        }
        if (ImGui::Checkbox("VSync", &vsync)){
            Application::Get().GetWindow().SetVSync(vsync);
        }
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Terrain Sculpt", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Checkbox("Sculpt Mode", &m_TerrainSculpt);
        ImGui::BeginDisabled(!m_TerrainSculpt);
        const char* modes[] = { "Raise", "Lower", "Smooth" };
        for (int i = 0; i < IM_ARRAYSIZE(modes); i++){
            if (i > 0) { ImGui::SameLine(); }
            ImGui::RadioButton(modes[i], &m_BrushMode, i);
        }
        ImGui::DragFloat("Radius", &m_BrushRadius, 0.1f, 0.5f, 20.0f);
        ImGui::DragFloat("Strength", &m_BrushStrength, 0.1f, 0.1f, 30.0f);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Select a Terrain, then drag in the viewport.");
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void MainLayer::DrawProfilerPanel(){
    if (!m_ShowProfiler)
        return;
    ImGui::Begin("Profiler", &m_ShowProfiler);
    const ImGuiIO& io = ImGui::GetIO();

    // Headline numbers.
    if (ImGui::BeginTable("##ProfilerStats", 3, ImGuiTableFlags_SizingStretchSame)){
        auto stat = [](const char* label, const char* value){
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);
            ImGui::PushFont(ImGuiLayer::GetFont(ImGuiLayer::FontType::Heading));
            ImGui::TextUnformatted(value);
            ImGui::PopFont();
        };
        char fps[32], ms[32], tris[32];
        std::snprintf(fps, sizeof(fps), "%.0f", io.Framerate);
        std::snprintf(ms, sizeof(ms), "%.2f ms", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        std::snprintf(tris, sizeof(tris), "%d", io.MetricsRenderIndices / 3);
        stat("FPS", fps);
        stat("Frame", ms);
        stat("UI Triangles", tris);
        ImGui::EndTable();
    }
    ImGui::Spacing();

    // Scope timings with a bar relative to the slowest scope.
    int maxTime = 1;
    for (const auto& p : Application::s_TimeContainer)
        maxTime = std::max(maxTime, p.second);
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##ProfilerScopes", 2, flags)){
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (const auto& p : Application::s_TimeContainer){
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.first.c_str());
            ImGui::TableNextColumn();
            char label[32];
            std::snprintf(label, sizeof(label), "%d us", p.second);
            ImGui::ProgressBar((float)p.second / (float)maxTime, ImVec2(-FLT_MIN, 0.0f), label);
        }
        ImGui::EndTable();
    }
    if (Application::s_TimeContainer.empty())
        ImGui::TextDisabled("No profiled scopes yet.");

    ImGui::Spacing();
    ImGui::TextDisabled("Dear ImGui %s", ImGui::GetVersion());
    ImGui::End();
}

void MainLayer::DrawShaderLibraryPanel(){
    if (!m_ShowShaderLibrary)
        return;
    ImGui::Begin("Shader Library", &m_ShowShaderLibrary);
    if (ImGui::Button("Import Shader...", ImVec2(-FLT_MIN, 0.0f)))
        m_OpenImportShader = true;
    ImGui::Spacing();

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
    if (ImGui::BeginTable("##Shaders", 1, flags)){
        ImGui::TableSetupColumn("Loaded shaders");
        ImGui::TableHeadersRow();
        for (auto iter = m_ShaderLibrary->GetBegin(); iter != m_ShaderLibrary->GetEnd(); ++iter){
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(iter->first.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void MainLayer::DrawHelpPopups(){
    // Opened from the menus / shortcuts via flags: OpenPopup must run in this ID scope.
    if (m_OpenImportShader){ ImGui::OpenPopup("Import Shader"); m_OpenImportShader = false; }
    if (m_OpenShortcuts)   { ImGui::OpenPopup("Keyboard Shortcuts"); m_OpenShortcuts = false; }
    if (m_OpenAbout)       { ImGui::OpenPopup("About Phoenix"); m_OpenAbout = false; }
    if (m_OpenGuide)       { ImGui::OpenPopup("AI Sandbox Guide"); m_OpenGuide = false; }

    if (file_dialog.showFileDialog("Import Shader", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".glsl"))
    {
        m_ShaderLibrary->Add(Shader::Create(file_dialog.selected_path));
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Keyboard Shortcuts", nullptr, ImGuiWindowFlags_NoSavedSettings)){
        struct Row { const char* action; const char* keys; };
        const Row rows[] = {
            { "Select / Move / Rotate / Scale", "Q / W / E / R" },
            { "Snap while dragging a gizmo",    "Hold Ctrl" },
            { "Orbit / Pan / Zoom camera",      "RMB / MMB / Wheel" },
            { "Select entity",                  "Left-click" },
            { "Deselect",                       "Esc" },
            { "Delete entity",                  "Del" },
            { "Undo / Redo",                    "Ctrl+Z / Ctrl+Shift+Z" },
            { "New / Import / Export scene",    "Ctrl+N / Ctrl+O / Ctrl+S" },
            { "Play / Stop",                    "F5" },
            { "Pause / Step one decision",      "F6 / F7" },
            { "Frame selection",                "F" },
            { "Drive a Keyboard-policy agent",  "WASD / arrows" },
            { "This window",                    "F1" },
        };
        if (ImGui::BeginTable("##Shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)){
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed);
            for (const Row& r : rows){
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(r.action);
                ImGui::TableNextColumn(); ImGui::TextDisabled("%s", r.keys);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("AI Sandbox Guide", nullptr, ImGuiWindowFlags_NoSavedSettings)){
        struct Step { const char* title; const char* text; };
        const Step steps[] = {
            { "1. Build the world",
              "Create > AI > Training Arena gives a walled floor. Add obstacles (Create > AI > Wall), goals and "
              "hazards (Reward Zones). Tags matter: agents target the nearest \"Goal\"; rays detect the tags you list." },
            { "2. Add an agent",
              "Create > AI > Agent. In Properties pick its control (Move / Force / Custom Lua), action space, "
              "observations (velocity, heading, target, rays, script values) and rewards." },
            { "3. Randomize",
              "Spawn Randomizer moves entities every episode so the policy generalizes instead of memorizing." },
            { "4. Test it yourself",
              "Set Policy to Keyboard, press Play (F5) and drive with WASD. Watch rewards in the Agent Monitor." },
            { "5. Connect your RL code",
              "Set Policy to Python, enable Training > Python Bridge, and drive the scene from any Gymnasium-based "
              "library (python/phoenix_env.py). The scene advances in lockstep with your step() calls." },
            { "6. Inspect",
              "Watch episode returns in the Training panel and observations in the Agent Monitor; export CSV." },
        };
        for (const Step& step : steps){
            ImGui::PushFont(ImGuiLayer::GetFont(ImGuiLayer::FontType::Bold));
            ImGui::TextUnformatted(step.title);
            ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", step.text);
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
        if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("About Phoenix", nullptr, ImGuiWindowFlags_NoSavedSettings)){
        ImVec2 p = ImGui::GetCursorScreenPos();
        EditorUI::DrawIcon(ImGui::GetWindowDrawList(), EditorUI::Icon::Logo, ImVec2(p.x + 20.0f, p.y + 22.0f), 36.0f, 0);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 52.0f, p.y + 4.0f));
        ImGui::BeginGroup();
        ImGui::PushFont(ImGuiLayer::GetFont(ImGuiLayer::FontType::Heading));
        ImGui::TextUnformatted("Phoenix Engine");
        ImGui::PopFont();
        ImGui::TextDisabled("AI sandbox for reinforcement-learning environments");
        ImGui::EndGroup();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped("Design environments for RL agents: sensors, rewards, randomization, an in-editor "
                           "Gymnasium bridge for your Python RL code, on a C++17 engine with an EnTT ECS, "
                           "Jolt physics, Lua scripting and an OpenGL renderer.");
        ImGui::TextDisabled("Dear ImGui %s", ImGui::GetVersion());
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}


void MainLayer::HandleShortcuts(){
    // Polled through ImGui rather than KeyPressed events: the ImGui layer swallows key
    // events whenever a panel (not the viewport) has focus, so shortcuts work anywhere.
    // Leave keys to a focused text field (its own Ctrl+Z, typing Q/W/E/R).
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;
    auto pressed = [](ImGuiKey key){ return ImGui::IsKeyPressed(key, false); };
    const bool control = io.KeyCtrl;

    // Undo / redo: Ctrl+Z and Ctrl+Shift+Z.
    if (control && pressed(ImGuiKey_Z)){
        if (io.KeyShift) { Redo(); }
        else             { Undo(); }
    }

    // Scene file shortcuts.
    if (control && pressed(ImGuiKey_N)) { NewScene(); }
    if (control && pressed(ImGuiKey_O)) { ImportScene(); }
    if (control && pressed(ImGuiKey_S)) { ExportScene(); }

    if (pressed(ImGuiKey_F5)) { TogglePlay(); }
    if (pressed(ImGuiKey_F6) && m_Scene->IsRunning()) { m_Paused = !m_Paused; }
    if (pressed(ImGuiKey_F7) && m_Environment){
        m_Paused = true;
        m_Environment->paused = true;
        m_Environment->StepDecision();
    }
    // Frame the selection (keeps the current viewing angle).
    if (!control && pressed(ImGuiKey_F)){
        Entity selected = m_SceneEditor->GetSelectedEntity();
        if (selected && selected.HasComponent<TransformComponent>()){
            const auto& t = selected.GetComponent<TransformComponent>();
            float size = std::max({ t.Scale.x, t.Scale.y, t.Scale.z });
            m_MainCamera.Focus(t.Translation, std::clamp(size * 2.5f, 4.0f, 120.0f));
        }
    }
    if (pressed(ImGuiKey_F1)) { m_OpenShortcuts = true; }

    // Esc deselects, unless it is closing a popup.
    if (pressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        m_SceneEditor->SetSelectedEntity({});

    // Gizmo operation shortcuts (only when not mid-manipulation).
    if (!control && !ImGuizmo::IsUsing()){
        if (pressed(ImGuiKey_Q)) { m_GizmoType = -1; } // none
        if (pressed(ImGuiKey_W)) { m_GizmoType = 0; }  // translate
        if (pressed(ImGuiKey_E)) { m_GizmoType = 1; }  // rotate
        if (pressed(ImGuiKey_R)) { m_GizmoType = 2; }  // scale
    }
}


AIPanelContext MainLayer::MakeAIContext() {
    AIPanelContext ctx;
    ctx.scene = m_Scene.get();
    ctx.environment = m_Environment.get();
    ctx.log = &m_EpisodeLog;
    ctx.bridge = &m_Bridge;
    ctx.selected = m_SceneEditor->GetSelectedEntity();
    ctx.timeScale = &m_TimeScale;
    ctx.maxSpeed = &m_MaxSpeed;
    ctx.paused = &m_Paused;
    ctx.bridgeEnabled = &m_BridgeEnabled;
    ctx.renderAtMaxSpeed = &m_RenderAtMaxSpeed;
    ctx.bridgePort = &m_BridgePort;
    ctx.togglePlay = [this](){ TogglePlay(); };
    ctx.select = [this](Entity e){ m_SceneEditor->SetSelectedEntity(e); };
    return ctx;
}

// Four identical arenas side by side: every walker agent evaluates its own candidate
// episode, so a client gets four episodes for the price of one (multi-agent API).
void MainLayer::BuildAIGoalShowcase() {
    m_Scene->AmbientColor() = glm::vec3(0.12f);
    {
        auto sun = m_Scene->CreateDirLightEntity("Sun");
        auto& t = sun.GetComponent<TransformComponent>();
        t.Translation = { 8.0f, 16.0f, 10.0f };
        t.Rotation = { glm::radians(-60.0f), glm::radians(25.0f), 0.0f };
        auto& dl = sun.GetComponent<DirLightComponent>();
        dl.ambient = glm::vec3(0.06f);
        dl.diffuse = glm::vec3(0.8f);
    }

    const float spacing = 20.0f;
    for (int i = 0; i < 4; i++){
        glm::vec3 c((i % 2) * spacing - spacing * 0.5f, 0.0f, (i / 2) * spacing - spacing * 0.5f);
        std::string suffix = " " + std::to_string(i + 1);
        AIPrefabs::CreateArena(*m_Scene, c, 16.0f, ("Arena" + suffix).c_str());

        auto agent = AIPrefabs::CreateAgent(*m_Scene, c + glm::vec3(0.0f, 0.45f, 4.0f), ("Agent" + suffix).c_str());
        agent.GetComponent<RandomizerComponent>().positionRange = { 5.0f, 0.0f, 2.0f };

        auto goal = AIPrefabs::CreateGoal(*m_Scene, c + glm::vec3(0.0f, 0.06f, -3.5f));
        auto& goalRandom = goal.AddComponent<RandomizerComponent>();
        goalRandom.positionRange = { 5.5f, 0.0f, 2.5f };
        goalRandom.randomYaw = false;

        auto hazard = AIPrefabs::CreateHazard(*m_Scene, c + glm::vec3(0.0f, 0.03f, 0.5f), { 2.5f, 1.2f });
        auto& hazardRandom = hazard.AddComponent<RandomizerComponent>();
        hazardRandom.positionRange = { 4.5f, 0.0f, 0.8f };
        hazardRandom.randomYaw = false;
    }

    m_MainCamera.Focus({ 0.0f, 0.0f, 1.0f }, 42.0f, 180.0f, -58.0f);
}

// Rolling balls on raised platforms: continuous force actions, fall-off penalty.
void MainLayer::BuildAIRollerShowcase() {
    m_Scene->AmbientColor() = glm::vec3(0.1f);
    {
        auto sun = m_Scene->CreateDirLightEntity("Sun");
        auto& t = sun.GetComponent<TransformComponent>();
        t.Translation = { 6.0f, 14.0f, 8.0f };
        t.Rotation = { glm::radians(-55.0f), glm::radians(35.0f), 0.0f };
    }

    const float spacing = 16.0f;
    for (int i = 0; i < 4; i++){
        glm::vec3 c((i % 2) * spacing - spacing * 0.5f, 0.0f, (i / 2) * spacing - spacing * 0.5f);
        std::string suffix = " " + std::to_string(i + 1);

        auto platform = m_Scene->CreateEntity(("Platform" + suffix).c_str());
        auto& t = platform.GetComponent<TransformComponent>();
        t.Translation = c + glm::vec3(0.0f, -0.25f, 0.0f);
        t.Scale = { 10.0f, 0.5f, 10.0f };
        auto& cube = platform.AddComponent<CubeComponent>();
        cube.material.ambient = glm::vec3(0.18f, 0.2f, 0.24f);
        cube.material.diffuse = glm::vec3(0.36f, 0.4f, 0.48f);
        cube.material.specular = glm::vec3(0.2f);
        cube.material.reflectivity = 0.1f;
        auto& rb = platform.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Static;
        rb.friction = 0.8f;
        platform.AddComponent<BoxColliderComponent>();

        auto ball = AIPrefabs::CreateRollerAgent(*m_Scene, c + glm::vec3(0.0f, 0.5f, 0.0f), ("Roller" + suffix).c_str());
        auto& ballRandom = ball.AddComponent<RandomizerComponent>();
        ballRandom.positionRange = { 3.5f, 0.0f, 3.5f };
        ballRandom.randomYaw = false;

        auto goal = AIPrefabs::CreateGoal(*m_Scene, c + glm::vec3(2.0f, 0.06f, -2.0f));
        goal.GetComponent<TransformComponent>().Scale = { 1.2f, 0.12f, 1.2f };
        auto& goalRandom = goal.AddComponent<RandomizerComponent>();
        goalRandom.positionRange = { 3.5f, 0.0f, 3.5f };
        goalRandom.randomYaw = false;
    }

    m_MainCamera.Focus({ 0.0f, 0.0f, 0.0f }, 32.0f, 180.0f, -55.0f);
}
