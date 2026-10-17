#pragma once
#include "CodeEditor.h"
#include <Phoenix/core/base.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <vendor/imFileDialog/ImGuiFileBrowser.h>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Phoenix{

    // A starting point for a new script ("New Script" menus).
    struct ScriptTemplate{
        const char* name;
        const char* description;
        const char* source;
    };
    const std::vector<ScriptTemplate>& GetScriptTemplates();

    // The Script Editor window: one tab per Lua script on the selected (or pinned)
    // entity, a code editor with live syntax checking, an API reference, the script
    // console, and hot reload while playing.
    class ScriptEditorPanel{
    public:
        void SetContext(const Ref<Scene>& scene);
        void OnImGuiRender(Entity selected, bool* open);

        // Bring script `index` of `entity` to the front (Properties "Edit").
        void Open(Entity entity, int index);

        // Adds a script made from `tmpl` (unique name) and returns its index. Reloads
        // the entity's runtimes when playing.
        static int AddScript(Entity entity, const ScriptTemplate& tmpl);
        // Script list edits that keep the per-script editor state (undo history,
        // cursor) attached to the right script, and reload while playing.
        void RemoveScript(Entity entity, int index);
        void MoveScript(Entity entity, int from, int to);
        void DuplicateScript(Entity entity, int index);
    private:
        struct Document{
            std::unique_ptr<CodeEditor> editor;
            std::string checkedText;  // text the syntax check last ran on
            bool checked = false;
            double pendingSince = -1.0;
            std::string syntaxError;  // "" when it compiles
            int syntaxLine = -1;
            std::string reloadedText; // live reload: last text sent to the runtime
        };
        using DocKey = std::pair<uint32_t, int>;

        Document& GetDocument(Entity entity, int index);
        void SyncDocument(Document& doc, Entity entity, int index);
        void ConfigureEditor(CodeEditor& editor) const;
        void Reload(Entity entity);
        void Flash(const std::string& message);
        void NewScriptMenuItems(Entity entity);

        void DrawMenuBar(Entity entity, int index, Document* doc);
        void DrawHeader(Entity entity);
        void DrawTabs(Entity entity);
        void DrawToolbar(Entity entity, int index, Document& doc);
        void DrawApiReference(CodeEditor& editor, Entity entity, int index);
        void DrawConsole(Entity entity, int index);
        void DrawStatusBar(Entity entity, int index, Document& doc);
        void DrawDialogs(Entity entity);
        void InsertApi(CodeEditor& editor, Entity entity, int index, const CodeEditor::ApiItem& item);
    private:
        Ref<Scene> m_Scene;
        std::map<DocKey, Document> m_Documents;
        std::vector<CodeEditor::ApiItem> m_Api;

        Entity m_Pinned;                 // shown instead of the selection when set
        int m_ActiveScript = 0;          // tab shown for the current entity
        uint32_t m_ActiveEntity = 0xffffffff;
        int m_SelectTab = -1;            // force this tab selected next frame
        DocKey m_PendingOpen{ 0xffffffff, -1 };
        bool m_WasRunning = false;

        // view options (shared by all scripts)
        bool m_ShowApi = true;
        bool m_ShowConsole = true;
        bool m_LiveReload = false;
        float m_ConsoleHeight = 150.0f;
        bool m_ConsoleNoRoom = false;    // console on, but the window is too short
        float m_ApiWidth = 270.0f;
        float m_Zoom = 1.0f;
        bool m_ShowLineNumbers = true, m_ShowIndentGuides = true, m_ShowWhitespace = false;
        bool m_AutoClosePairs = true, m_AutoCloseBlocks = true, m_AutoComplete = true;

        // API reference / console filters
        char m_ApiFilter[64] = "";
        char m_ConsoleFilter[64] = "";
        int m_ConsoleScope = 0;          // 0 this entity, 1 this script, 2 all
        bool m_ConsoleErrorsOnly = false;
        std::size_t m_ConsoleSeen = 0;   // entries shown last frame (auto-scroll)

        // rename / file dialogs
        int m_RenameIndex = -1;
        char m_RenameBuf[64] = "";
        bool m_OpenRename = false;
        bool m_OpenImport = false, m_OpenExport = false;
        int m_ExportIndex = -1;
        std::string m_StatusMessage;     // transient status-bar message
        double m_StatusTime = -10.0;
        int m_PendingGoToLine = -1;      // jump after switching tab (console click)
        imgui_addons::ImGuiFileBrowser m_FileDialog;
    };
}
