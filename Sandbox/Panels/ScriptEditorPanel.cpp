#include "ScriptEditorPanel.h"
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <Phoenix/imGui/ImGuiLayer.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Scene/LuaScript.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Phoenix{

    namespace{
        const ImVec4 kRed(0.94f, 0.40f, 0.40f, 1.0f);
        const ImVec4 kGreen(0.45f, 0.80f, 0.45f, 1.0f);
        const ImVec4 kAmber(0.95f, 0.75f, 0.30f, 1.0f);

        bool ContainsNoCase(const std::string& haystack, const char* needle){
            if (!needle || !*needle) { return true; }
            auto it = std::search(haystack.begin(), haystack.end(), needle, needle + std::strlen(needle),
                [](char a, char b){ return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
            return it != haystack.end();
        }

        // "Name(a, b [, c]) -> r" -> "a, b" (optional parameters dropped).
        std::string RequiredParams(const std::string& signature){
            std::size_t open = signature.find('('), close = signature.find(')', open == std::string::npos ? 0 : open);
            if (open == std::string::npos || close == std::string::npos) { return ""; }
            std::string params = signature.substr(open + 1, close - open - 1);
            std::size_t opt = params.find('[');
            if (opt != std::string::npos) { params = params.substr(0, opt); }
            while (!params.empty() && (params.back() == ' ' || params.back() == ',')) { params.pop_back(); }
            return params;
        }

        int CountLines(const std::string& s){ return 1 + (int)std::count(s.begin(), s.end(), '\n'); }
    }

    const std::vector<ScriptTemplate>& GetScriptTemplates(){
        static const std::vector<ScriptTemplate> templates = {
            { "Script", "An empty OnUpdate.",
                "-- runs while playing (press Run)\n"
                "function OnUpdate(dt)\n"
                "    \n"
                "end\n" },
            { "Spin", "Turns the entity around its Y axis.",
                "local speed = 1.5 -- radians per second\n"
                "\n"
                "function OnUpdate(dt)\n"
                "    Rotate(0, speed * dt, 0)\n"
                "end\n" },
            { "Bob", "Floats up and down around its start position.",
                "local height = 0.5 -- units\n"
                "local speed = 2.0\n"
                "local t = 0.0\n"
                "local x, y, z = GetTranslation()\n"
                "\n"
                "function OnUpdate(dt)\n"
                "    t = t + dt\n"
                "    SetTranslation(x, y + math.sin(t * speed) * height, z)\n"
                "end\n" },
            { "Color Cycle", "Cycles the material colour through the rainbow.",
                "local t = 0.0\n"
                "\n"
                "function OnUpdate(dt)\n"
                "    t = t + dt\n"
                "    SetColor(0.5 + 0.5 * math.sin(t), 0.5 + 0.5 * math.sin(t + 2.0), 0.5 + 0.5 * math.sin(t + 4.0))\n"
                "end\n" },
            { "WASD Mover", "W/S move along the facing, A/D turn.",
                "local speed = 3.0 -- units per second\n"
                "local turn = 2.0  -- radians per second\n"
                "\n"
                "function OnUpdate(dt)\n"
                "    if IsKeyDown(Key.W) then MoveForward(speed * dt) end\n"
                "    if IsKeyDown(Key.S) then MoveForward(-speed * dt) end\n"
                "    if IsKeyDown(Key.A) then Rotate(0, turn * dt, 0) end\n"
                "    if IsKeyDown(Key.D) then Rotate(0, -turn * dt, 0) end\n"
                "end\n" },
            { "Jump", "Space launches the body upwards (needs a Rigid Body).",
                "local jumpSpeed = 6.0\n"
                "\n"
                "function OnUpdate(dt)\n"
                "    if IsKeyPressed(Key.Space) then\n"
                "        local vx, vy, vz = GetVelocity()\n"
                "        SetVelocity(vx, jumpSpeed, vz)\n"
                "    end\n"
                "end\n" },
            { "Collision Glow", "Glows while touching something (needs a Rigid Body + collider).",
                "function OnCollisionEnter(other)\n"
                "    print(\"touching \" .. other)\n"
                "    SetEmissive(1.0, 0.4, 0.1, 4.0)\n"
                "end\n"
                "\n"
                "function OnCollisionExit(other)\n"
                "    SetEmissive(0.0, 0.0, 0.0, 0.0)\n"
                "end\n" },
            { "Animation Keys", "1 / 2 cross-fade between the first two clips.",
                "local fade = 0.3 -- seconds\n"
                "\n"
                "function OnUpdate(dt)\n"
                "    if IsKeyPressed(Key[\"1\"]) then CrossFade(0, fade) end\n"
                "    if IsKeyPressed(Key[\"2\"]) then CrossFade(1, fade) end\n"
                "end\n"
                "\n"
                "function OnAnimationEvent(name)\n"
                "    print(\"animation event: \" .. name)\n"
                "end\n" },
            { "Ground Probe", "Ray-casts downwards and prints what is below.",
                "function OnUpdate(dt)\n"
                "    local x, y, z = GetTranslation()\n"
                "    local tag, hx, hy, hz = RayCast(x, y, z, 0, -1, 0, 50)\n"
                "    if tag then\n"
                "        print(string.format(\"%s is %.2f below\", tag, y - hy))\n"
                "    end\n"
                "end\n" },
        };
        return templates;
    }

    // ===================================================================== script list

    int ScriptEditorPanel::AddScript(Entity entity, const ScriptTemplate& tmpl){
        if (!entity.HasComponent<LuaScriptComponent>())
            entity.AddComponent<LuaScriptComponent>().scripts.clear();
        auto& comp = entity.GetComponent<LuaScriptComponent>();
        auto taken = [&](const std::string& n){
            return std::any_of(comp.scripts.begin(), comp.scripts.end(), [&](const LuaScriptEntry& s){ return s.name == n; });
        };
        std::string name = tmpl.name;
        for (int n = 2; taken(name); n++) { name = std::string(tmpl.name) + " " + std::to_string(n); }
        comp.Add(name, tmpl.source);
        if (Scene* scene = entity.GetScene()) { scene->ReloadScripts(entity); }
        return (int)comp.scripts.size() - 1;
    }

    void ScriptEditorPanel::RemoveScript(Entity entity, int index){
        auto& scripts = entity.GetComponent<LuaScriptComponent>().scripts;
        if (index < 0 || index >= (int)scripts.size()) { return; }
        scripts.erase(scripts.begin() + index);
        // Shift the editors of the scripts after it down by one.
        const uint32_t e = (uint32_t)entity;
        m_Documents.erase({ e, index });
        for (int i = index + 1; i <= (int)scripts.size(); i++){
            auto node = m_Documents.extract({ e, i });
            if (!node.empty()) { node.key() = { e, i - 1 }; m_Documents.insert(std::move(node)); }
        }
        if (m_ActiveScript >= (int)scripts.size()) { m_ActiveScript = std::max(0, (int)scripts.size() - 1); }
        m_SelectTab = m_ActiveScript;
        if (m_Scene) { m_Scene->ReloadScripts(entity); }
    }

    void ScriptEditorPanel::MoveScript(Entity entity, int from, int to){
        auto& scripts = entity.GetComponent<LuaScriptComponent>().scripts;
        if (from < 0 || to < 0 || from >= (int)scripts.size() || to >= (int)scripts.size() || from == to) { return; }
        std::swap(scripts[from], scripts[to]);
        const uint32_t e = (uint32_t)entity;
        auto a = m_Documents.extract({ e, from });
        auto b = m_Documents.extract({ e, to });
        if (!a.empty()) { a.key() = { e, to }; m_Documents.insert(std::move(a)); }
        if (!b.empty()) { b.key() = { e, from }; m_Documents.insert(std::move(b)); }
        m_SelectTab = to;
        if (m_Scene) { m_Scene->ReloadScripts(entity); }
    }

    void ScriptEditorPanel::DuplicateScript(Entity entity, int index){
        auto& scripts = entity.GetComponent<LuaScriptComponent>().scripts;
        if (index < 0 || index >= (int)scripts.size()) { return; }
        const uint32_t e = (uint32_t)entity;
        for (int i = (int)scripts.size() - 1; i > index; i--){
            auto node = m_Documents.extract({ e, i });
            if (!node.empty()) { node.key() = { e, i + 1 }; m_Documents.insert(std::move(node)); }
        }
        LuaScriptEntry copy = scripts[index];
        copy.name += " Copy";
        scripts.insert(scripts.begin() + index + 1, copy);
        m_SelectTab = index + 1;
        if (m_Scene) { m_Scene->ReloadScripts(entity); }
    }

    // ===================================================================== documents

    void ScriptEditorPanel::SetContext(const Ref<Scene>& scene){
        m_Scene = scene;
        m_Documents.clear();
        m_Pinned = {};
        m_ActiveEntity = 0xffffffff;
        m_ActiveScript = 0;
    }

    void ScriptEditorPanel::Open(Entity entity, int index){
        m_PendingOpen = { (uint32_t)entity, index };
        if (m_Pinned && m_Pinned != entity) { m_Pinned = entity; }
    }

    void ScriptEditorPanel::ConfigureEditor(CodeEditor& editor) const{
        editor.font = ImGuiLayer::GetMonospaceFont();
        editor.zoom = m_Zoom;
        editor.showLineNumbers = m_ShowLineNumbers;
        editor.showIndentGuides = m_ShowIndentGuides;
        editor.showWhitespace = m_ShowWhitespace;
        editor.autoClosePairs = m_AutoClosePairs;
        editor.autoCloseBlocks = m_AutoCloseBlocks;
        editor.autoComplete = m_AutoComplete;
    }

    ScriptEditorPanel::Document& ScriptEditorPanel::GetDocument(Entity entity, int index){
        Document& doc = m_Documents[{ (uint32_t)entity, index }];
        if (!doc.editor){
            doc.editor = std::make_unique<CodeEditor>();
            doc.editor->SetApi(m_Api);
            doc.editor->SetText(entity.GetComponent<LuaScriptComponent>().scripts[index].source);
            doc.reloadedText = doc.editor->GetText();
        }
        return doc;
    }

    void ScriptEditorPanel::SyncDocument(Document& doc, Entity entity, int index){
        CodeEditor& editor = *doc.editor;
        const std::string& source = entity.GetComponent<LuaScriptComponent>().scripts[index].source;
        // Changed outside the editor (scene undo, import): adopt it as an undoable step.
        if (source != editor.GetText()) { editor.SetText(source, true); }

        // Syntax check, debounced while typing.
        const std::string& text = editor.GetText();
        const double now = ImGui::GetTime();
        if (!doc.checked || text != doc.checkedText){
            if (doc.pendingSince < 0.0) { doc.pendingSince = now; }
            if (!doc.checked || now - doc.pendingSince > 0.25){
                doc.syntaxError.clear();
                doc.syntaxLine = -1;
                LuaScript::CheckSyntax(text, doc.syntaxError, doc.syntaxLine);
                doc.checkedText = text;
                doc.checked = true;
                doc.pendingSince = -1.0;
            }
        }

        const bool running = m_Scene && m_Scene->IsRunning();
        if (m_LiveReload && running && doc.syntaxError.empty() && doc.checkedText == text && doc.reloadedText != text){
            doc.reloadedText = text;
            Reload(entity);
        }

        std::vector<CodeEditor::Diagnostic> diagnostics;
        if (!doc.syntaxError.empty() && doc.syntaxLine > 0)
            diagnostics.push_back({ doc.syntaxLine, doc.syntaxError, true });
        if (running){
            LuaScript* rs = m_Scene->FindScript(entity, index);
            if (rs && !rs->IsValid() && rs->GetErrorLine() > 0 && doc.syntaxError.empty())
                diagnostics.push_back({ rs->GetErrorLine(), "runtime: " + rs->GetError(), true });
        }
        editor.SetDiagnostics(diagnostics);
    }

    void ScriptEditorPanel::Reload(Entity entity){
        if (!m_Scene || !m_Scene->IsRunning()) { return; }
        m_Scene->ReloadScripts(entity);
        Flash("Reloaded scripts on " + entity.GetComponent<TagComponent>().Tag);
    }

    void ScriptEditorPanel::Flash(const std::string& message){
        m_StatusMessage = message;
        m_StatusTime = ImGui::GetTime();
    }

    // ===================================================================== window

    void ScriptEditorPanel::OnImGuiRender(Entity selected, bool* open){
        if (!*open || !m_Scene) { return; }
        if (m_Api.empty()){
            for (const LuaApiDoc& d : LuaScript::GetApiDocs()){
                CodeEditor::ItemKind kind = d.category == "Callbacks" ? CodeEditor::ItemKind::Callback
                                          : d.category == "Keys"      ? CodeEditor::ItemKind::Constant
                                                                      : CodeEditor::ItemKind::Function;
                m_Api.push_back({ d.name, d.signature, d.doc, kind });
            }
            m_Api.push_back({ "Key", "Key", "Key codes: Key.W, Key.Space, Key.Up, Key[\"1\"] ...", CodeEditor::ItemKind::Module });
        }

        const bool running = m_Scene->IsRunning();
        if (running && !m_WasRunning){
            // Play just started from the current text: nothing to live-reload yet.
            for (auto& [key, doc] : m_Documents) { if (doc.editor) { doc.reloadedText = doc.editor->GetText(); } }
        }
        m_WasRunning = running;

        if (m_Pinned && !m_Scene->IsValid(m_Pinned)) { m_Pinned = {}; }
        Entity entity = m_Pinned ? m_Pinned : selected;
        if (entity && !m_Scene->IsValid(entity)) { entity = {}; }
        if (m_PendingOpen.second >= 0 && entity && (uint32_t)entity == m_PendingOpen.first){
            m_SelectTab = m_PendingOpen.second;
            m_PendingOpen = { 0xffffffff, -1 };
        }
        if ((uint32_t)entity != m_ActiveEntity){
            m_ActiveEntity = (uint32_t)entity;
            m_ActiveScript = 0;
            if (m_SelectTab < 0) { m_SelectTab = 0; }
        }

        // Hidden (behind another dock tab or collapsed): keep pending requests for later.
        if (!ImGui::Begin("Script Editor", open, ImGuiWindowFlags_MenuBar)){
            ImGui::End();
            return;
        }

        if (!entity){
            DrawMenuBar(entity, -1, nullptr);
            ImGui::TextDisabled("Select an entity to edit its Lua scripts.");
            ImGui::End();
            return;
        }

        if (!entity.HasComponent<LuaScriptComponent>() || entity.GetComponent<LuaScriptComponent>().scripts.empty()){
            DrawMenuBar(entity, -1, nullptr);
            ImGui::Spacing();
            ImGui::TextDisabled("This entity has no scripts.");
            if (ImGui::Button("Add Script...")) { ImGui::OpenPopup("##addfirst"); }
            if (ImGui::BeginPopup("##addfirst")){
                NewScriptMenuItems(entity);
                ImGui::EndPopup();
            }
            DrawDialogs(entity);
            ImGui::End();
            return;
        }

        DrawTabs(entity);
        // Tab actions may have changed the list.
        if (!entity.HasComponent<LuaScriptComponent>() || entity.GetComponent<LuaScriptComponent>().scripts.empty()){
            DrawDialogs(entity);
            ImGui::End();
            return;
        }
        auto& scripts = entity.GetComponent<LuaScriptComponent>().scripts;
        const int index = std::max(0, std::min(m_ActiveScript, (int)scripts.size() - 1));
        Document& doc = GetDocument(entity, index);
        ConfigureEditor(*doc.editor);
        SyncDocument(doc, entity, index);
        if (m_PendingGoToLine > 0){
            doc.editor->GoToLine(m_PendingGoToLine);
            m_PendingGoToLine = -1;
        }

        DrawToolbar(entity, index, doc);

        // Layout: [editor | API reference] / splitter / console / status bar.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float statusH = ImGui::GetFrameHeight();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float splitterH = 6.0f;
        const float editorMin = 100.0f;
        // The console takes what the editor can spare, and hides when that's too little.
        const float consoleRoom = avail.y - statusH - splitterH - editorMin - style.ItemSpacing.y * 3.0f;
        const bool showConsole = m_ShowConsole && consoleRoom >= 70.0f;
        m_ConsoleNoRoom = m_ShowConsole && !showConsole;
        const float consoleH = showConsole ? std::max(70.0f, std::min(m_ConsoleHeight, consoleRoom)) + splitterH : 0.0f;
        const float editorH = std::max(editorMin * 0.5f, avail.y - consoleH - statusH - style.ItemSpacing.y * 2.0f);
        const float apiW = m_ShowApi ? std::min(m_ApiWidth, avail.x * 0.45f) : 0.0f;
        const float editorW = avail.x - (m_ShowApi ? apiW + style.ItemSpacing.x : 0.0f);

        ImGui::BeginGroup();
        if (doc.editor->Render("##editor", ImVec2(editorW, editorH)))
            scripts[index].source = doc.editor->GetText();
        ImGui::EndGroup();
        m_Zoom = doc.editor->zoom; // Ctrl+wheel zoom applies to every script
        if (doc.editor->ConsumeSaveRequest()){
            if (running) { Reload(entity); }
            else { Flash("Edits apply live; save the scene (File > Save) to keep them on disk."); }
        }

        if (m_ShowApi){
            ImGui::SameLine();
            ImGui::BeginChild("##api", ImVec2(apiW, editorH), true);
            DrawApiReference(*doc.editor, entity, index);
            ImGui::EndChild();
        }

        if (showConsole){
            // Drag the bar between the editor and the console to resize them.
            ImGui::InvisibleButton("##consolesplit", ImVec2(-FLT_MIN, splitterH));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); }
            if (ImGui::IsItemActive())
                m_ConsoleHeight = std::max(70.0f, std::min(consoleH - splitterH - ImGui::GetIO().MouseDelta.y, consoleRoom));
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, (a.y + b.y) * 0.5f), ImVec2(b.x, (a.y + b.y) * 0.5f),
                ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGuiCol_Separator));
            DrawConsole(entity, index);
        }

        DrawStatusBar(entity, index, doc);
        DrawMenuBar(entity, index, &doc);
        DrawDialogs(entity);
        ImGui::End();
    }

    // Right side of the menu bar: which entity is shown, pinning, play state.
    void ScriptEditorPanel::DrawHeader(Entity entity){
        const std::string& tag = entity.GetComponent<TagComponent>().Tag;
        const char* state = m_Scene->IsRunning() ? "Playing" : "Edit mode";
        const ImGuiStyle& style = ImGui::GetStyle();
        const float w = ImGui::CalcTextSize(tag.c_str()).x + ImGui::CalcTextSize(m_Pinned ? "Pinned" : "Pin").x
                      + ImGui::CalcTextSize(state).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x * 4.0f;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w));
        ImGui::TextUnformatted(tag.c_str());
        if (m_Pinned){
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::SmallButton("Pinned")) { m_Pinned = {}; }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Click to follow the selection again."); }
        }
        else{
            if (ImGui::SmallButton("Pin")) { m_Pinned = entity; }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Keep showing this entity's scripts when the selection changes."); }
        }

        if (m_Scene->IsRunning()) { ImGui::TextColored(kGreen, "%s", state); }
        else{
            ImGui::TextDisabled("%s", state);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Scripts run when you press Run."); }
        }
    }

    void ScriptEditorPanel::NewScriptMenuItems(Entity entity){
        for (const ScriptTemplate& t : GetScriptTemplates()){
            if (ImGui::MenuItem(t.name)){
                m_SelectTab = AddScript(entity, t);
                m_ActiveScript = m_SelectTab;
            }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", t.description); }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import .lua File...")) { m_OpenImport = true; }
    }

    void ScriptEditorPanel::DrawTabs(Entity entity){
        auto& scripts = entity.GetComponent<LuaScriptComponent>().scripts;
        enum class Action { None, Remove, Duplicate, MoveLeft, MoveRight };
        Action action = Action::None;
        int actionIndex = -1;

        const ImGuiTabBarFlags barFlags = ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton;
        if (ImGui::BeginTabBar("##scripts", barFlags)){
            for (int i = 0; i < (int)scripts.size(); i++){
                LuaScriptEntry& script = scripts[i];
                auto it = m_Documents.find({ (uint32_t)entity, i });
                std::string syntaxError = (it != m_Documents.end()) ? it->second.syntaxError : "";
                LuaScript* rs = m_Scene->IsRunning() ? m_Scene->FindScript(entity, i) : nullptr;
                bool runtimeError = rs && !rs->IsValid();

                std::string label = script.name + (script.enabled ? "" : " (off)") + "###script" + std::to_string(i);
                ImGuiTabItemFlags flags = (m_SelectTab == i) ? ImGuiTabItemFlags_SetSelected : 0;
                int colors = 0;
                if (!syntaxError.empty() || runtimeError) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); colors++; }
                else if (!script.enabled) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); colors++; }
                bool visible = ImGui::BeginTabItem(label.c_str(), nullptr, flags);
                ImGui::PopStyleColor(colors);

                if (ImGui::IsItemHovered()){
                    ImGui::BeginTooltip();
                    ImGui::Text("%s  (%d lines)", script.name.c_str(), CountLines(script.source));
                    ImGui::TextDisabled(script.enabled ? "Enabled" : "Disabled: kept but not run");
                    if (!syntaxError.empty()) { ImGui::TextColored(kRed, "Syntax error: %s", syntaxError.c_str()); }
                    if (runtimeError) { ImGui::TextColored(kRed, "Stopped: %s", rs->GetError().c_str()); }
                    ImGui::TextDisabled("Right-click for options");
                    ImGui::EndTooltip();
                }
                if (ImGui::BeginPopupContextItem()){
                    if (ImGui::MenuItem("Rename...")){
                        m_RenameIndex = i;
                        std::snprintf(m_RenameBuf, sizeof(m_RenameBuf), "%s", script.name.c_str());
                        m_OpenRename = true;
                    }
                    if (ImGui::MenuItem("Enabled", nullptr, &script.enabled)) { Reload(entity); }
                    if (ImGui::MenuItem("Duplicate")) { action = Action::Duplicate; actionIndex = i; }
                    if (ImGui::MenuItem("Move Left", nullptr, false, i > 0)) { action = Action::MoveLeft; actionIndex = i; }
                    if (ImGui::MenuItem("Move Right", nullptr, false, i + 1 < (int)scripts.size())) { action = Action::MoveRight; actionIndex = i; }
                    if (ImGui::MenuItem("Export .lua File...")) { m_ExportIndex = i; m_OpenExport = true; }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove")) { action = Action::Remove; actionIndex = i; }
                    ImGui::EndPopup();
                }
                if (visible){
                    m_ActiveScript = i;
                    ImGui::EndTabItem();
                }
            }
            m_SelectTab = -1;

            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
                ImGui::OpenPopup("##newscript");
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("New script"); }
            if (ImGui::BeginPopup("##newscript")){
                NewScriptMenuItems(entity);
                ImGui::EndPopup();
            }
            ImGui::EndTabBar();
        }

        switch (action){
            case Action::Remove:    RemoveScript(entity, actionIndex); break;
            case Action::Duplicate: DuplicateScript(entity, actionIndex); break;
            case Action::MoveLeft:  MoveScript(entity, actionIndex, actionIndex - 1); break;
            case Action::MoveRight: MoveScript(entity, actionIndex, actionIndex + 1); break;
            default: break;
        }
    }

    void ScriptEditorPanel::DrawToolbar(Entity entity, int index, Document& doc){
        auto& script = entity.GetComponent<LuaScriptComponent>().scripts[index];
        const bool running = m_Scene->IsRunning();

        if (ImGui::Checkbox("Enabled", &script.enabled)) { Reload(entity); }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Disabled scripts are kept but not run."); }
        ImGui::SameLine();
        ImGui::BeginDisabled(!running);
        if (ImGui::Button("Reload")) { Reload(entity); }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(running ? "Restart this entity's scripts with the current code (Ctrl+S)."
                                      : "Hot reload is available while playing.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!running);
        ImGui::Checkbox("Live", &m_LiveReload);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("While playing, reload automatically whenever the script compiles.");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("Find")) { doc.editor->OpenFind(false); }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Find / replace (Ctrl+F, Ctrl+H)"); }
        ImGui::SameLine();
        ImGui::BeginDisabled(!doc.editor->CanUndo());
        if (ImGui::Button("Undo")) { doc.editor->Undo(); script.source = doc.editor->GetText(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!doc.editor->CanRedo());
        if (ImGui::Button("Redo")) { doc.editor->Redo(); script.source = doc.editor->GetText(); }
        ImGui::EndDisabled();

        // Panel toggles on the right.
        const float w = ImGui::CalcTextSize("API").x + ImGui::CalcTextSize("Console").x
                      + ImGui::GetStyle().FramePadding.x * 4.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w));
        auto toggle = [](const char* label, bool* v){
            if (*v) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]); }
            if (ImGui::Button(label)) { *v = !*v; }
            if (*v) { ImGui::PopStyleColor(); }
        };
        toggle("API", &m_ShowApi);
        ImGui::SameLine();
        toggle("Console", &m_ShowConsole);
        if (m_ConsoleNoRoom && ImGui::IsItemHovered())
            ImGui::SetTooltip("Hidden: not enough height. Make the Script Editor taller to see the console.");
    }

    void ScriptEditorPanel::InsertApi(CodeEditor& editor, Entity entity, int index, const CodeEditor::ApiItem& item){
        auto& source = entity.GetComponent<LuaScriptComponent>().scripts[index].source;
        using K = CodeEditor::ItemKind;
        if (item.kind == K::Callback){
            // Jump to the callback if it's already defined, else append it.
            std::string head = "function " + item.name + "(";
            std::size_t at = editor.GetText().find(head);
            if (at != std::string::npos){
                editor.GoToLine(CountLines(editor.GetText().substr(0, at)) + 1);
                return;
            }
            std::string text = editor.GetText();
            if (!text.empty() && text.back() != '\n') { text += '\n'; }
            if (!text.empty()) { text += '\n'; }
            std::size_t open = item.signature.find('('), close = item.signature.find(')');
            std::string params = (open != std::string::npos && close != std::string::npos)
                               ? item.signature.substr(open + 1, close - open - 1) : "";
            text += "function " + item.name + "(" + params + ")\n    \nend\n";
            editor.SetText(text, true);
            editor.GoToLine(CountLines(text) - 2);
        }
        else if (item.kind == K::Function && item.signature.find('(') != std::string::npos){
            editor.InsertText(item.name + "(" + RequiredParams(item.signature) + ")");
            editor.Focus();
        }
        else{
            editor.InsertText(item.name);
            editor.Focus();
        }
        source = editor.GetText();
    }

    void ScriptEditorPanel::DrawApiReference(CodeEditor& editor, Entity entity, int index){
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##apifilter", "Search API...", m_ApiFilter, sizeof(m_ApiFilter));
        ImGui::TextDisabled("Double-click to insert");
        ImGui::Separator();
        ImGui::BeginChild("##apilist");

        // Engine API by category (in binding order), then the Lua standard library.
        std::vector<std::pair<std::string, std::vector<const CodeEditor::ApiItem*>>> groups;
        auto groupFor = [&](const std::string& name) -> std::vector<const CodeEditor::ApiItem*>&{
            for (auto& g : groups) { if (g.first == name) { return g.second; } }
            groups.push_back({ name, {} });
            return groups.back().second;
        };
        const auto& docs = LuaScript::GetApiDocs();
        for (std::size_t i = 0; i < docs.size() && i < m_Api.size(); i++) { groupFor(docs[i].category).push_back(&m_Api[i]); }
        for (const CodeEditor::ApiItem& b : editor.GetBuiltins()){
            if (b.kind == CodeEditor::ItemKind::Module) { continue; }
            std::size_t dot = b.name.find('.');
            groupFor(dot == std::string::npos ? std::string("Lua") : "Lua " + b.name.substr(0, dot)).push_back(&b);
        }

        const bool filtering = m_ApiFilter[0] != 0;
        for (auto& [category, items] : groups){
            std::vector<const CodeEditor::ApiItem*> shown;
            for (const auto* it : items)
                if (ContainsNoCase(it->name, m_ApiFilter) || ContainsNoCase(it->doc, m_ApiFilter)) { shown.push_back(it); }
            if (shown.empty()) { continue; }
            if (filtering) { ImGui::SetNextItemOpen(true); }
            ImGuiTreeNodeFlags flags = (category.rfind("Lua", 0) == 0 || category == "Keys") ? 0 : ImGuiTreeNodeFlags_DefaultOpen;
            if (!ImGui::TreeNodeEx(category.c_str(), flags | ImGuiTreeNodeFlags_SpanAvailWidth)) { continue; }
            for (const auto* it : shown){
                ImGui::PushID(it->name.c_str());
                if (ImGui::Selectable(it->name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)
                    && ImGui::IsMouseDoubleClicked(0))
                    InsertApi(editor, entity, index, *it);
                if (ImGui::IsItemHovered()){
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                    if (ImFont* mono = ImGuiLayer::GetMonospaceFont()) { ImGui::PushFont(mono); }
                    ImGui::TextColored(ImVec4(0.38f, 0.69f, 0.94f, 1.0f), "%s", it->signature.c_str());
                    if (ImGuiLayer::GetMonospaceFont()) { ImGui::PopFont(); }
                    if (!it->doc.empty()) { ImGui::TextUnformatted(it->doc.c_str()); }
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::EndChild();
    }

    void ScriptEditorPanel::DrawConsole(Entity entity, int index){
        // Fill the space left above the status bar.
        ImGui::BeginChild("##console", ImVec2(0.0f, -(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y)), true);
        const auto& log = m_Scene->GetScriptLog();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Console");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        const char* scopes[] = { "This entity", "This script", "All scripts" };
        ImGui::Combo("##scope", &m_ConsoleScope, scopes, IM_ARRAYSIZE(scopes));
        ImGui::SameLine();
        ImGui::Checkbox("Errors only", &m_ConsoleErrorsOnly);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputTextWithHint("##consolefilter", "Filter...", m_ConsoleFilter, sizeof(m_ConsoleFilter));
        ImGui::SameLine();
        if (ImGui::Button("Clear")) { m_Scene->ClearScriptLog(); }

        std::vector<int> rows;
        int errors = 0;
        for (int i = 0; i < (int)log.size(); i++){
            const ScriptLogEntry& e = log[i];
            if (m_ConsoleScope <= 1 && e.entity != (entt::entity)entity) { continue; }
            if (m_ConsoleScope == 1 && e.script != index) { continue; }
            if (e.error) { errors++; }
            if (m_ConsoleErrorsOnly && !e.error) { continue; }
            if (!ContainsNoCase(e.text, m_ConsoleFilter) && !ContainsNoCase(e.scriptName, m_ConsoleFilter)) { continue; }
            rows.push_back(i);
        }
        ImGui::SameLine();
        if (errors) { ImGui::TextColored(kRed, "%d error%s", errors, errors == 1 ? "" : "s"); }
        else { ImGui::TextDisabled("%d message%s", (int)rows.size(), rows.size() == 1 ? "" : "s"); }

        ImGui::Separator();
        ImGui::BeginChild("##log", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        if (rows.empty()){
            ImGui::TextDisabled(m_Scene->IsRunning() ? "No output yet. print(...) writes here."
                                                     : "Press Run: print(...) output and script errors appear here.");
        }
        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
        ImGuiListClipper clipper;
        clipper.Begin((int)rows.size());
        while (clipper.Step()){
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++){
                const ScriptLogEntry& e = log[rows[r]];
                std::string text = e.text;
                std::replace(text.begin(), text.end(), '\n', ' ');
                char prefix[160];
                if (m_ConsoleScope == 2)
                    std::snprintf(prefix, sizeof(prefix), "[%7.2fs] %s / %s: ", e.time, e.entityName.c_str(), e.scriptName.c_str());
                else
                    std::snprintf(prefix, sizeof(prefix), "[%7.2fs] %s: ", e.time, e.scriptName.c_str());
                std::string row = prefix + text;
                if (e.count > 1) { row += "  (x" + std::to_string(e.count) + ")"; }

                ImGui::PushID(rows[r]);
                if (e.error) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); }
                bool clicked = ImGui::Selectable(row.c_str(), false);
                if (e.error) { ImGui::PopStyleColor(); }
                if (e.line > 0 && ImGui::IsItemHovered()) { ImGui::SetTooltip("Go to %s, line %d", e.scriptName.c_str(), e.line); }
                if (clicked && e.line > 0){
                    Entity target{ e.entity, m_Scene.get() };
                    if (m_Scene->IsValid(target)){
                        if (target != entity) { m_Pinned = target; }
                        m_SelectTab = e.script;
                        m_PendingGoToLine = e.line;
                    }
                }
                if (ImGui::BeginPopupContextItem()){
                    if (ImGui::MenuItem("Copy")) { ImGui::SetClipboardText(e.text.c_str()); }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
        if (log.size() != m_ConsoleSeen && atBottom) { ImGui::SetScrollHereY(1.0f); }
        m_ConsoleSeen = log.size();
        ImGui::EndChild();
        ImGui::EndChild();
    }

    void ScriptEditorPanel::DrawStatusBar(Entity entity, int index, Document& doc){
        CodeEditor& editor = *doc.editor;
        const CodeEditor::Coord c = editor.GetCursor();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Ln %d, Col %d", c.line + 1, c.col + 1);
        if (int sel = editor.GetSelectedCharCount()){
            ImGui::SameLine();
            ImGui::TextDisabled("(%d selected)", sel);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (!doc.syntaxError.empty()){
            ImGui::TextColored(kRed, "Line %d: %s", doc.syntaxLine, doc.syntaxError.c_str());
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Click to go to the error"); }
            if (ImGui::IsItemClicked() && doc.syntaxLine > 0) { editor.GoToLine(doc.syntaxLine); }
        }
        else{
            ImGui::TextColored(kGreen, "No syntax errors");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        const auto& script = entity.GetComponent<LuaScriptComponent>().scripts[index];
        if (!m_Scene->IsRunning()) { ImGui::TextDisabled(script.enabled ? "Runs on Play" : "Disabled"); }
        else if (LuaScript* rs = m_Scene->FindScript(entity, index)){
            if (rs->IsValid()) { ImGui::TextColored(kGreen, "Running"); }
            else{
                if (rs->GetErrorLine() > 0) { ImGui::TextColored(kRed, "Stopped at line %d: %s", rs->GetErrorLine(), rs->GetError().c_str()); }
                else { ImGui::TextColored(kRed, "Stopped: %s", rs->GetError().c_str()); }
                if (ImGui::IsItemClicked() && rs->GetErrorLine() > 0) { editor.GoToLine(rs->GetErrorLine()); }
            }
        }
        else { ImGui::TextDisabled(script.enabled ? "Not running" : "Disabled"); }

        if (!m_StatusMessage.empty() && ImGui::GetTime() - m_StatusTime < 4.0){
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextColored(kAmber, "%s", m_StatusMessage.c_str());
        }

        char right[64];
        std::snprintf(right, sizeof(right), "%d lines   Lua   Spaces: 4   %d%%", editor.GetLineCount(), (int)std::lround(editor.zoom * 100.0f));
        float w = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - w));
        ImGui::TextDisabled("%s", right);
    }

    void ScriptEditorPanel::DrawMenuBar(Entity entity, int index, Document* doc){
        if (!ImGui::BeginMenuBar()) { return; }
        const bool hasEntity = (bool)entity;
        const bool hasScript = doc && index >= 0;
        CodeEditor* ed = hasScript ? doc->editor.get() : nullptr;
        auto writeBack = [&]{
            if (ed) { entity.GetComponent<LuaScriptComponent>().scripts[index].source = ed->GetText(); }
        };

        if (ImGui::BeginMenu("Script")){
            if (ImGui::BeginMenu("New Script", hasEntity)){
                NewScriptMenuItems(entity);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Export .lua File...", nullptr, false, hasScript)) { m_ExportIndex = index; m_OpenExport = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Rename...", nullptr, false, hasScript)){
                m_RenameIndex = index;
                std::snprintf(m_RenameBuf, sizeof(m_RenameBuf), "%s", entity.GetComponent<LuaScriptComponent>().scripts[index].name.c_str());
                m_OpenRename = true;
            }
            if (hasScript){
                bool& enabled = entity.GetComponent<LuaScriptComponent>().scripts[index].enabled;
                if (ImGui::MenuItem("Enabled", nullptr, &enabled)) { Reload(entity); }
            }
            if (ImGui::MenuItem("Duplicate", nullptr, false, hasScript)) { DuplicateScript(entity, index); }
            if (ImGui::MenuItem("Remove", nullptr, false, hasScript)) { RemoveScript(entity, index); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit", hasScript)){
            bool acted = true;
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, ed->CanUndo())) { ed->Undo(); writeBack(); }
            else if (ImGui::MenuItem("Redo", "Ctrl+Y", false, ed->CanRedo())) { ed->Redo(); writeBack(); }
            else if (ImGui::MenuItem("Cut", "Ctrl+X")) { ed->Cut(); writeBack(); }
            else if (ImGui::MenuItem("Copy", "Ctrl+C")) { ed->Copy(); }
            else if (ImGui::MenuItem("Paste", "Ctrl+V")) { ed->Paste(); writeBack(); }
            else if (ImGui::MenuItem("Select All", "Ctrl+A")) { ed->SelectAll(); }
            else if (ImGui::MenuItem("Toggle Comment", "Ctrl+/")) { ed->ToggleComment(); writeBack(); }
            else { acted = false; }
            ImGui::Separator();
            if (ImGui::MenuItem("Find", "Ctrl+F")) { ed->OpenFind(false); }
            if (ImGui::MenuItem("Replace", "Ctrl+H")) { ed->OpenFind(true); }
            if (ImGui::MenuItem("Go to Line", "Ctrl+G")) { ed->OpenGoToLine(); }
            if (acted) { ed->Focus(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")){
            ImGui::MenuItem("Line Numbers", nullptr, &m_ShowLineNumbers);
            ImGui::MenuItem("Indent Guides", nullptr, &m_ShowIndentGuides);
            ImGui::MenuItem("Whitespace", nullptr, &m_ShowWhitespace);
            ImGui::Separator();
            ImGui::MenuItem("Auto-close Brackets and Quotes", nullptr, &m_AutoClosePairs);
            ImGui::MenuItem("Auto-insert 'end'", nullptr, &m_AutoCloseBlocks);
            ImGui::MenuItem("Suggestions While Typing", nullptr, &m_AutoComplete);
            ImGui::Separator();
            if (ImGui::MenuItem("Zoom In", "Ctrl+=")) { m_Zoom = std::min(3.0f, m_Zoom + 0.1f); }
            if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) { m_Zoom = std::max(0.5f, m_Zoom - 0.1f); }
            if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) { m_Zoom = 1.0f; }
            ImGui::Separator();
            ImGui::MenuItem("API Reference", nullptr, &m_ShowApi);
            ImGui::MenuItem("Console", nullptr, &m_ShowConsole);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Run")){
            const bool running = m_Scene->IsRunning();
            if (ImGui::MenuItem("Reload Entity Scripts", "Ctrl+S", false, running && hasEntity)) { Reload(entity); }
            ImGui::MenuItem("Live Reload", nullptr, &m_LiveReload);
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Console")) { m_Scene->ClearScriptLog(); }
            ImGui::EndMenu();
        }
        // Keyboard help.
        if (ImGui::BeginMenu("Help")){
            const char* keys[][2] = {
                { "Ctrl+Space", "Suggestions" }, { "Tab / Enter", "Accept suggestion" },
                { "Ctrl+F / Ctrl+H", "Find / Replace" }, { "F3 / Shift+F3", "Next / previous match" },
                { "Ctrl+G", "Go to line" }, { "Ctrl+/", "Toggle comment" },
                { "Ctrl+D", "Duplicate line / selection" }, { "Ctrl+Shift+K", "Delete line" },
                { "Alt+Up / Alt+Down", "Move lines" }, { "Tab / Shift+Tab", "Indent / unindent" },
                { "Ctrl+L", "Select line" }, { "Ctrl+Enter", "New line below" },
                { "Ctrl+Z / Ctrl+Y", "Undo / redo" }, { "Ctrl+Wheel, Ctrl+= / -", "Zoom" },
                { "Ctrl+S", "Reload while playing" }, { "Double / triple click", "Select word / line" },
            };
            if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingFixedFit)){
                for (auto& k : keys){
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(k[0]);
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k[1]);
                }
                ImGui::EndTable();
            }
            ImGui::EndMenu();
        }
        if (hasEntity) { DrawHeader(entity); }
        ImGui::EndMenuBar();
    }

    void ScriptEditorPanel::DrawDialogs(Entity entity){
        // Rename
        if (m_OpenRename) { ImGui::OpenPopup("Rename Script"); m_OpenRename = false; }
        if (ImGui::BeginPopup("Rename Script")){
            if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
            ImGui::SetNextItemWidth(220.0f);
            bool apply = ImGui::InputText("##rename", m_RenameBuf, sizeof(m_RenameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::SameLine();
            apply |= ImGui::Button("OK");
            if (apply){
                if (entity && entity.HasComponent<LuaScriptComponent>()){
                    auto& scripts = entity.GetComponent<LuaScriptComponent>().scripts;
                    if (m_RenameIndex >= 0 && m_RenameIndex < (int)scripts.size() && m_RenameBuf[0])
                        scripts[m_RenameIndex].name = m_RenameBuf;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Import: a .lua file becomes a new script named after it.
        if (m_OpenImport) { ImGui::OpenPopup("Import Lua Script"); m_OpenImport = false; }
        if (m_FileDialog.showFileDialog("Import Lua Script", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".lua")){
            std::ifstream file(m_FileDialog.selected_path, std::ios::binary);
            if (!file) { Flash("Could not read " + m_FileDialog.selected_path); }
            else if (entity){
                std::stringstream ss;
                ss << file.rdbuf();
                std::string source = ss.str();
                std::string name = std::filesystem::path(m_FileDialog.selected_path).stem().string();
                ScriptTemplate t{ name.c_str(), "", source.c_str() };
                m_SelectTab = AddScript(entity, t);
                Flash("Imported " + m_FileDialog.selected_fn);
            }
        }

        // Export
        if (m_OpenExport) { ImGui::OpenPopup("Export Lua Script"); m_OpenExport = false; }
        if (m_FileDialog.showFileDialog("Export Lua Script", imgui_addons::ImGuiFileBrowser::DialogMode::SAVE, ImVec2(700, 310), ".lua")){
            if (entity && entity.HasComponent<LuaScriptComponent>()){
                auto& scripts = entity.GetComponent<LuaScriptComponent>().scripts;
                if (m_ExportIndex >= 0 && m_ExportIndex < (int)scripts.size()){
                    std::filesystem::path path = m_FileDialog.selected_path;
                    if (path.extension() != ".lua") { path += ".lua"; }
                    std::ofstream file(path, std::ios::binary);
                    if (file) { file << scripts[m_ExportIndex].source; Flash("Exported to " + path.string()); }
                    else { Flash("Could not write " + path.string()); }
                }
            }
        }
    }
}
