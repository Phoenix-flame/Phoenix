#pragma once
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace Phoenix{

    // A Lua code editor widget for ImGui: syntax highlighting, line numbers, undo/redo,
    // selection with mouse and keyboard, find/replace, go-to-line, auto-indent and
    // bracket/block closing, comment toggling, line moving, completion with signature
    // and hover help, and error markers.
    //
    // Text is stored as lines of UTF-8; a column is a byte offset into its line.
    class CodeEditor{
    public:
        struct Coord{
            int line = 0, col = 0;
            bool operator==(const Coord& o) const { return line == o.line && col == o.col; }
            bool operator!=(const Coord& o) const { return !(*this == o); }
            bool operator<(const Coord& o) const { return line != o.line ? line < o.line : col < o.col; }
            bool operator>(const Coord& o) const { return o < *this; }
            bool operator<=(const Coord& o) const { return !(o < *this); }
            bool operator>=(const Coord& o) const { return !(*this < o); }
        };

        // A problem shown on a line (1-based): tinted line, gutter marker, inline text.
        struct Diagnostic{
            int line = 0;
            std::string message;
            bool error = true; // false: warning
        };

        enum class ItemKind { Function, Callback, Constant, Keyword, Module, Variable, Snippet };

        // A name the editor knows about (engine API, Lua builtins): completion
        // candidate, hover help and signature help.
        struct ApiItem{
            std::string name;      // "SetTranslation", "math.sin", "Key.Space"
            std::string signature; // "SetTranslation(x, y, z)"
            std::string doc;
            ItemKind kind = ItemKind::Function;
        };

        CodeEditor();

        // Replaces the text. With keepUndo the replacement is one undoable step,
        // otherwise the undo history is cleared.
        void SetText(const std::string& text, bool keepUndo = false);
        const std::string& GetText() const;

        // Draws the editor (find bar + text area) filling `size` (0 = available
        // space). Returns true if the text was edited this frame.
        bool Render(const char* id, const ImVec2& size = ImVec2(0.0f, 0.0f));

        void SetDiagnostics(const std::vector<Diagnostic>& diagnostics) { m_Diagnostics = diagnostics; }
        // Engine API names: highlighted, completed and documented. Lua's own
        // keywords, standard library and snippets are built in.
        void SetApi(const std::vector<ApiItem>& api);
        const std::vector<ApiItem>& GetBuiltins() const;

        // Editing commands (also bound to keys while focused).
        void InsertText(const std::string& text); // at the cursor, replacing the selection
        void Undo();
        void Redo();
        bool CanUndo() const { return m_UndoIndex > 0; }
        bool CanRedo() const { return m_UndoIndex < (int)m_Undo.size(); }
        void Copy();
        void Cut();
        void Paste();
        void SelectAll();
        void ToggleComment();
        void OpenFind(bool withReplace);
        void OpenGoToLine();
        void GoToLine(int line); // 1-based; selects nothing, centers the line
        void Focus() { m_WantFocus = true; }

        bool IsFocused() const { return m_Focused; }
        Coord GetCursor() const { return m_Cursor; }
        int GetLineCount() const { return (int)m_Lines.size(); }
        int GetSelectedCharCount() const;
        // True once per Ctrl+S press while focused (the caller decides what saving means).
        bool ConsumeSaveRequest() { bool r = m_SaveRequested; m_SaveRequested = false; return r; }

        // Options
        bool showLineNumbers = true;
        bool showIndentGuides = true;
        bool showWhitespace = false;
        bool highlightCurrentLine = true;
        bool autoClosePairs = true;   // (), [], {}, quotes
        bool autoCloseBlocks = true;  // `end` after function/if/for/while
        bool autoComplete = true;     // popup while typing (Ctrl+Space always works)
        float zoom = 1.0f;
        int tabSize = 4;
        ImFont* font = nullptr;       // null: current font
    private:
        enum class Tok : unsigned char { Default, Keyword, Constant, Api, Builtin, Function, Self, String, Number, Comment, Punct };
        struct Token { int start, end; Tok kind; };
        enum class EditKind { Other, Typing, Deleting };
        struct UndoRecord{
            std::string before, after;
            Coord beforeCursor, beforeAnchor, afterCursor, afterAnchor;
            EditKind kind = EditKind::Other;
            double time = 0.0;
        };
        struct Match { Coord start, end; };
        struct CompletionEntry { std::string label, insert, signature, doc; ItemKind kind; int score; };

        // ---- text model ----
        void SetLines(const std::string& text);
        std::string JoinLines() const;
        std::string GetRange(Coord a, Coord b) const;
        void DeleteRange(Coord a, Coord b);
        Coord InsertAt(Coord at, const std::string& text);
        bool HasSelection() const { return m_Cursor != m_Anchor; }
        Coord SelStart() const { return m_Cursor < m_Anchor ? m_Cursor : m_Anchor; }
        Coord SelEnd() const { return m_Cursor < m_Anchor ? m_Anchor : m_Cursor; }
        void ReplaceSelection(const std::string& text);
        Coord Clamp(Coord c) const;
        int NextCharCol(int line, int col) const;
        int PrevCharCol(int line, int col) const;
        Coord WordStart(Coord c) const;
        Coord WordEnd(Coord c) const;
        Coord MoveWordLeft(Coord c) const;
        Coord MoveWordRight(Coord c) const;
        static int LeadingSpaces(const std::string& s);
        void SetCursor(Coord c, bool extend);
        void MarkChanged();

        // ---- undo ----
        void BeginEdit(EditKind kind);
        void EndEdit();

        // ---- editing ----
        void TypeChar(unsigned int c);
        void NewLine();
        void Backspace(bool word);
        void DeleteForward(bool word);
        void IndentLines(bool unindent);
        void MoveLines(int dir);
        void DuplicateSelection();
        void DeleteLines();
        void AutoDedent();
        bool LineOpensBlock(const std::string& text, bool* byKeyword) const;
        int BlockBalance() const;

        // ---- syntax ----
        int Tokenize(const std::string& line, int state, std::vector<Token>* out) const;
        void UpdateLineStates();
        Tok KindAt(Coord c) const;
        bool FindMatchingBracket(Coord at, Coord& outOpen, Coord& outClose) const;

        // ---- find ----
        void UpdateMatches();
        void FindNext(bool forward);
        void ReplaceCurrent();
        void ReplaceAll();
        void DrawFindBar();
        void DrawGoToBar();

        // ---- completion / help ----
        void UpdateCompletion(bool force);
        void AcceptCompletion();
        const ApiItem* LookupApi(const std::string& name) const;
        std::string IdentifierAt(Coord c, Coord* start = nullptr, Coord* end = nullptr) const;
        bool CallContext(std::string& outName, int& outArg) const;

        // ---- layout / drawing ----
        float TextWidth(const char* b, const char* e) const;
        float XOfCol(int line, int col) const;
        int ColOfX(int line, float x) const;
        Coord ScreenToCoord(const ImVec2& p) const;
        void HandleKeyboard();
        void HandleMouse(bool hovered, const ImRect& textRect);
        void ScrollToCursor();
        void DrawText(ImDrawList* dl, int first, int last);
        void DrawGutter(ImDrawList* dl, int first, int last);
        void DrawCompletion(ImDrawList* dl);
        void DrawSignatureHelp(ImDrawList* dl);
        void DrawHoverHelp();
        void DrawContextMenu();
    private:
        std::vector<std::string> m_Lines;
        mutable std::string m_TextCache;
        mutable bool m_TextDirty = true;
        int m_Version = 0;          // bumps on every change
        bool m_Changed = false;     // edited during this Render

        Coord m_Cursor, m_Anchor;
        float m_PreferredX = -1.0f; // keeps the column on vertical moves

        std::vector<UndoRecord> m_Undo;
        int m_UndoIndex = 0;        // records [0, index) are applied
        UndoRecord m_Pending;
        bool m_MergeEdit = false;

        std::vector<int> m_LineStates; // lexer state at the start of each line
        int m_StatesVersion = -1;
        int m_LongestLine = 0;          // in characters

        std::vector<ApiItem> m_Api;
        std::unordered_map<std::string, int> m_ApiIndex; // name -> m_Api / builtin
        std::unordered_set<std::string> m_ApiNames;       // highlighted as engine API
        std::vector<Diagnostic> m_Diagnostics;

        // interaction
        bool m_Focused = false, m_WantFocus = false;
        bool m_Selecting = false;
        int m_SelectMode = 0; // 0 chars, 1 words, 2 lines
        Coord m_SelectOriginStart, m_SelectOriginEnd;
        bool m_ScrollToCursor = false;
        bool m_CenterCursor = false;
        double m_CaretTime = 0.0;
        bool m_SaveRequested = false;
        bool m_OpenContextMenu = false;
        ImVec2 m_HoverPos{ -1.0f, -1.0f };
        double m_HoverStart = 0.0;

        // layout (valid during Render)
        ImVec2 m_Origin;           // screen position of content (0,0), scroll applied
        float m_LineHeight = 16.0f, m_CharWidth = 8.0f, m_FontSize = 15.0f;
        ImFont* m_DrawFont = nullptr; // code font used for measuring
        float m_GutterWidth = 40.0f, m_TextStart = 48.0f;
        ImRect m_TextRect;         // visible area of the text child
        float m_ScrollX = 0.0f, m_ScrollY = 0.0f;

        // find / replace / go to
        bool m_FindOpen = false, m_ReplaceOpen = false, m_FocusFind = false;
        bool m_FindWasActive = false;
        char m_FindBuf[256] = "";
        char m_ReplaceBuf[256] = "";
        bool m_MatchCase = false, m_WholeWord = false, m_UseRegex = false;
        bool m_RegexError = false;
        std::vector<Match> m_Matches;
        std::string m_MatchesQuery;
        int m_MatchesVersion = -1, m_MatchesFlags = -1;
        bool m_GoToOpen = false, m_FocusGoTo = false;
        char m_GoToBuf[16] = "";

        // completion
        bool m_CompOpen = false;
        bool m_CompExplicit = false; // opened with Ctrl+Space: fuzzy matches too
        std::vector<CompletionEntry> m_CompItems;
        int m_CompSel = 0, m_CompScroll = 0;
        Coord m_CompStart;          // start of the word being completed
        ImRect m_CompRect;          // last drawn popup (mouse hit testing)
        float m_CompRowHeight = 0.0f;
    };
}
