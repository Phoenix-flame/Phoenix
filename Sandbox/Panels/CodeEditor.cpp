#include "CodeEditor.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <regex>

namespace Phoenix{

    namespace{
        const float kTopPad = 4.0f;  // space above the first line
        const int kCompRows = 8;     // visible rows in the completion popup

        bool IsIdentChar(char c){ return std::isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }
        bool IsIdentStart(char c){ return std::isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }
        bool IsBlank(const std::string& s){ return s.find_first_not_of(' ') == std::string::npos; }

        ImU32 Rgba(float r, float g, float b, float a = 1.0f){ return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a)); }

        // One Dark-ish palette.
        struct Palette{
            ImU32 bg          = Rgba(0.110f, 0.118f, 0.137f);
            ImU32 gutterBg    = Rgba(0.094f, 0.101f, 0.118f);
            ImU32 gutterSep   = Rgba(1.0f, 1.0f, 1.0f, 0.06f);
            ImU32 lineNumber  = Rgba(0.40f, 0.43f, 0.49f);
            ImU32 lineNumberHi= Rgba(0.82f, 0.84f, 0.88f);
            ImU32 currentLine = Rgba(1.0f, 1.0f, 1.0f, 0.045f);
            ImU32 selection   = Rgba(0.26f, 0.42f, 0.70f, 0.45f);
            ImU32 occurrence  = Rgba(1.0f, 1.0f, 1.0f, 0.09f);
            ImU32 findMatch   = Rgba(0.90f, 0.62f, 0.20f, 0.28f);
            ImU32 findCurrent = Rgba(0.95f, 0.70f, 0.25f, 0.85f);
            ImU32 bracket     = Rgba(0.85f, 0.85f, 0.85f, 0.55f);
            ImU32 indentGuide = Rgba(1.0f, 1.0f, 1.0f, 0.07f);
            ImU32 whitespace  = Rgba(1.0f, 1.0f, 1.0f, 0.16f);
            ImU32 caret       = Rgba(0.53f, 0.75f, 1.0f);
            ImU32 error       = Rgba(0.94f, 0.33f, 0.33f);
            ImU32 errorBg     = Rgba(0.94f, 0.33f, 0.33f, 0.10f);
            ImU32 warning     = Rgba(0.95f, 0.75f, 0.30f);
            ImU32 warningBg   = Rgba(0.95f, 0.75f, 0.30f, 0.08f);
            ImU32 popupBg     = Rgba(0.145f, 0.155f, 0.185f, 0.98f);
            ImU32 popupBorder = Rgba(1.0f, 1.0f, 1.0f, 0.12f);
            ImU32 popupSel    = Rgba(0.22f, 0.33f, 0.52f);
            ImU32 accent      = Rgba(0.38f, 0.69f, 0.94f);
            ImU32 dim         = Rgba(0.55f, 0.58f, 0.64f);
            ImU32 text        = Rgba(0.82f, 0.84f, 0.88f);
        };
        const Palette& Colors(){ static Palette p; return p; }

        ImU32 TokenColor(int kind){
            switch (kind){
                case 1:  return Rgba(0.776f, 0.471f, 0.867f); // keyword
                case 2:  return Rgba(0.820f, 0.604f, 0.400f); // constant
                case 3:  return Rgba(0.337f, 0.714f, 0.761f); // engine API
                case 4:  return Rgba(0.898f, 0.753f, 0.482f); // Lua builtin
                case 5:  return Rgba(0.380f, 0.686f, 0.937f); // function name
                case 6:  return Rgba(0.878f, 0.424f, 0.459f); // self
                case 7:  return Rgba(0.596f, 0.765f, 0.475f); // string
                case 8:  return Rgba(0.820f, 0.604f, 0.400f); // number
                case 9:  return Rgba(0.470f, 0.500f, 0.560f); // comment
                case 10: return Rgba(0.640f, 0.680f, 0.740f); // punctuation
                default: return Rgba(0.840f, 0.850f, 0.880f);
            }
        }

        ImU32 KindColor(CodeEditor::ItemKind k){
            switch (k){
                case CodeEditor::ItemKind::Function: return Rgba(0.337f, 0.714f, 0.761f);
                case CodeEditor::ItemKind::Callback: return Rgba(0.776f, 0.471f, 0.867f);
                case CodeEditor::ItemKind::Constant: return Rgba(0.820f, 0.604f, 0.400f);
                case CodeEditor::ItemKind::Keyword:  return Rgba(0.776f, 0.471f, 0.867f);
                case CodeEditor::ItemKind::Module:   return Rgba(0.898f, 0.753f, 0.482f);
                case CodeEditor::ItemKind::Snippet:  return Rgba(0.596f, 0.765f, 0.475f);
                default:                             return Rgba(0.60f, 0.63f, 0.68f);
            }
        }
        const char* KindLetter(CodeEditor::ItemKind k){
            switch (k){
                case CodeEditor::ItemKind::Function: return "f";
                case CodeEditor::ItemKind::Callback: return "e";
                case CodeEditor::ItemKind::Constant: return "c";
                case CodeEditor::ItemKind::Keyword:  return "k";
                case CodeEditor::ItemKind::Module:   return "m";
                case CodeEditor::ItemKind::Snippet:  return "s";
                default:                             return "v";
            }
        }

        const std::unordered_set<std::string>& LuaKeywords(){
            static const std::unordered_set<std::string> kw = {
                "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
                "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
            };
            return kw;
        }

        struct SnippetDef { const char* label; const char* body; const char* doc; };
        const SnippetDef s_Snippets[] = {
            { "fori",   "for i = 1, $0 do\n\t\nend",              "Numeric for loop" },
            { "forp",   "for k, v in pairs($0) do\n\t\nend",      "Loop over every key of a table" },
            { "forip",  "for i, v in ipairs($0) do\n\t\nend",     "Loop over an array" },
            { "ifb",    "if $0 then\n\t\nend",                    "if block" },
            { "ifelse", "if $0 then\n\t\nelse\n\t\nend",          "if / else block" },
            { "whileb", "while $0 do\n\t\nend",                   "while loop" },
            { "func",   "function $0()\n\t\nend",                 "Function" },
            { "lfunc",  "local function $0()\n\t\nend",           "Local function" },
        };

        // Long bracket "[==[" at s[i] -> its level (number of '='), or -1.
        int LongBracketLevel(const std::string& s, std::size_t i){
            if (i >= s.size() || s[i] != '[') { return -1; }
            std::size_t j = i + 1;
            while (j < s.size() && s[j] == '=') { j++; }
            return (j < s.size() && s[j] == '[') ? (int)(j - i - 1) : -1;
        }
        // Index just past the closing "]==]" of the given level, or npos.
        std::size_t FindLongClose(const std::string& s, std::size_t from, int level){
            for (std::size_t i = from; i < s.size(); i++){
                if (s[i] != ']') { continue; }
                std::size_t j = i + 1;
                int eq = 0;
                while (j < s.size() && s[j] == '=') { j++; eq++; }
                if (eq == level && j < s.size() && s[j] == ']') { return j + 1; }
            }
            return std::string::npos;
        }

        std::string ToLower(std::string s){
            for (char& c : s) { c = (char)std::tolower((unsigned char)c); }
            return s;
        }

        // Completion ranking: prefix (exact case) > prefix > substring > subsequence.
        int Score(const std::string& label, const std::string& query){
            if (query.empty()) { return 1; }
            if (label.compare(0, query.size(), query) == 0) { return 1000 - (int)label.size(); }
            std::string l = ToLower(label), q = ToLower(query);
            if (l.compare(0, q.size(), q) == 0) { return 800 - (int)label.size(); }
            std::size_t pos = l.find(q);
            if (pos != std::string::npos) { return 500 - (int)pos - (int)label.size(); }
            std::size_t qi = 0;
            for (char c : l) { if (qi < q.size() && c == q[qi]) { qi++; } }
            return qi == q.size() ? 200 - (int)label.size() : -1;
        }

        int Utf8Length(const std::string& s){
            int n = 0;
            for (char c : s) { if (((unsigned char)c & 0xC0) != 0x80) { n++; } }
            return n;
        }

        // Parameters of "Name(a, b [, c]) -> r" as written between the parentheses.
        std::string SignatureParams(const std::string& sig){
            std::size_t open = sig.find('('), close = sig.find(')', open == std::string::npos ? 0 : open);
            if (open == std::string::npos || close == std::string::npos) { return ""; }
            return sig.substr(open + 1, close - open - 1);
        }
    }

    // ===================================================================== text

    CodeEditor::CodeEditor(){
        m_Lines.emplace_back();
        SetApi({});
    }

    void CodeEditor::SetLines(const std::string& text){
        m_Lines.clear();
        std::string cur;
        for (char c : text){
            if (c == '\r') { continue; }
            if (c == '\n') { m_Lines.push_back(std::move(cur)); cur.clear(); }
            else if (c == '\t') { cur.append(tabSize, ' '); }
            else { cur.push_back(c); }
        }
        m_Lines.push_back(std::move(cur));
    }

    std::string CodeEditor::JoinLines() const{
        std::string out;
        for (std::size_t i = 0; i < m_Lines.size(); i++){
            if (i) { out.push_back('\n'); }
            out += m_Lines[i];
        }
        return out;
    }

    const std::string& CodeEditor::GetText() const{
        if (m_TextDirty){
            m_TextCache = JoinLines();
            m_TextDirty = false;
        }
        return m_TextCache;
    }

    void CodeEditor::SetText(const std::string& text, bool keepUndo){
        if (text == GetText()) { return; }
        bool changed = m_Changed;
        if (keepUndo){
            BeginEdit(EditKind::Other);
            SetLines(text);
            m_Cursor = m_Anchor = Clamp(m_Cursor);
            EndEdit();
        }
        else{
            SetLines(text);
            m_Undo.clear();
            m_UndoIndex = 0;
            m_Cursor = m_Anchor = Coord{};
            MarkChanged();
        }
        m_Changed = changed; // external text isn't an edit the caller needs to write back
        m_CompOpen = false;
    }

    void CodeEditor::MarkChanged(){
        m_TextDirty = true;
        m_Version++;
        m_Changed = true;
    }

    CodeEditor::Coord CodeEditor::Clamp(Coord c) const{
        c.line = std::max(0, std::min(c.line, (int)m_Lines.size() - 1));
        const std::string& s = m_Lines[c.line];
        c.col = std::max(0, std::min(c.col, (int)s.size()));
        while (c.col > 0 && c.col < (int)s.size() && ((unsigned char)s[c.col] & 0xC0) == 0x80) { c.col--; }
        return c;
    }

    int CodeEditor::NextCharCol(int line, int col) const{
        const std::string& s = m_Lines[line];
        if (col >= (int)s.size()) { return (int)s.size(); }
        col++;
        while (col < (int)s.size() && ((unsigned char)s[col] & 0xC0) == 0x80) { col++; }
        return col;
    }

    int CodeEditor::PrevCharCol(int line, int col) const{
        const std::string& s = m_Lines[line];
        if (col <= 0) { return 0; }
        col--;
        while (col > 0 && ((unsigned char)s[col] & 0xC0) == 0x80) { col--; }
        return col;
    }

    std::string CodeEditor::GetRange(Coord a, Coord b) const{
        if (b < a) { std::swap(a, b); }
        if (a.line == b.line) { return m_Lines[a.line].substr(a.col, b.col - a.col); }
        std::string out = m_Lines[a.line].substr(a.col);
        for (int l = a.line + 1; l < b.line; l++) { out += '\n'; out += m_Lines[l]; }
        out += '\n';
        out += m_Lines[b.line].substr(0, b.col);
        return out;
    }

    void CodeEditor::DeleteRange(Coord a, Coord b){
        if (b < a) { std::swap(a, b); }
        if (a == b) { return; }
        if (a.line == b.line){
            m_Lines[a.line].erase(a.col, b.col - a.col);
            return;
        }
        m_Lines[a.line] = m_Lines[a.line].substr(0, a.col) + m_Lines[b.line].substr(b.col);
        m_Lines.erase(m_Lines.begin() + a.line + 1, m_Lines.begin() + b.line + 1);
    }

    CodeEditor::Coord CodeEditor::InsertAt(Coord at, const std::string& text){
        std::vector<std::string> pieces(1);
        for (char c : text){
            if (c == '\r') { continue; }
            if (c == '\n') { pieces.emplace_back(); }
            else if (c == '\t') { pieces.back().append(tabSize, ' '); }
            else { pieces.back().push_back(c); }
        }
        std::string& line = m_Lines[at.line];
        if (pieces.size() == 1){
            line.insert(at.col, pieces[0]);
            return { at.line, at.col + (int)pieces[0].size() };
        }
        std::string tail = line.substr(at.col);
        line = line.substr(0, at.col) + pieces[0];
        int endCol = (int)pieces.back().size();
        pieces.back() += tail;
        m_Lines.insert(m_Lines.begin() + at.line + 1, pieces.begin() + 1, pieces.end());
        return { at.line + (int)pieces.size() - 1, endCol };
    }

    void CodeEditor::ReplaceSelection(const std::string& text){
        Coord s = SelStart();
        if (HasSelection()) { DeleteRange(s, SelEnd()); }
        m_Cursor = m_Anchor = InsertAt(s, text);
    }

    void CodeEditor::SetCursor(Coord c, bool extend){
        m_Cursor = Clamp(c);
        if (!extend) { m_Anchor = m_Cursor; }
        m_ScrollToCursor = true;
        m_CaretTime = ImGui::GetTime();
    }

    CodeEditor::Coord CodeEditor::WordStart(Coord c) const{
        const std::string& s = m_Lines[c.line];
        while (c.col > 0 && IsIdentChar(s[c.col - 1])) { c.col--; }
        return c;
    }

    CodeEditor::Coord CodeEditor::WordEnd(Coord c) const{
        const std::string& s = m_Lines[c.line];
        while (c.col < (int)s.size() && IsIdentChar(s[c.col])) { c.col++; }
        return c;
    }

    CodeEditor::Coord CodeEditor::MoveWordLeft(Coord c) const{
        if (c.col == 0) { return c.line > 0 ? Coord{ c.line - 1, (int)m_Lines[c.line - 1].size() } : c; }
        const std::string& s = m_Lines[c.line];
        while (c.col > 0 && s[c.col - 1] == ' ') { c.col--; }
        if (c.col > 0 && IsIdentChar(s[c.col - 1])) { return WordStart(c); }
        while (c.col > 0 && !IsIdentChar(s[c.col - 1]) && s[c.col - 1] != ' ') { c.col--; }
        return c;
    }

    CodeEditor::Coord CodeEditor::MoveWordRight(Coord c) const{
        const std::string& s = m_Lines[c.line];
        if (c.col >= (int)s.size()) { return c.line + 1 < (int)m_Lines.size() ? Coord{ c.line + 1, 0 } : c; }
        if (IsIdentChar(s[c.col])) { c = WordEnd(c); }
        else{
            while (c.col < (int)s.size() && !IsIdentChar(s[c.col]) && s[c.col] != ' ') { c.col++; }
        }
        while (c.col < (int)s.size() && s[c.col] == ' ') { c.col++; }
        return c;
    }

    int CodeEditor::LeadingSpaces(const std::string& s){
        int n = 0;
        while (n < (int)s.size() && s[n] == ' ') { n++; }
        return n;
    }

    int CodeEditor::GetSelectedCharCount() const{
        return HasSelection() ? Utf8Length(GetRange(SelStart(), SelEnd())) : 0;
    }

    // ===================================================================== undo

    void CodeEditor::BeginEdit(EditKind kind){
        double now = ImGui::GetTime();
        m_MergeEdit = kind != EditKind::Other && m_UndoIndex > 0 && m_UndoIndex == (int)m_Undo.size()
            && m_Undo.back().kind == kind && now - m_Undo.back().time < 1.0
            && m_Undo.back().afterCursor == m_Cursor && !HasSelection();
        if (!m_MergeEdit){
            m_Pending = UndoRecord{};
            m_Pending.before = GetText();
            m_Pending.beforeCursor = m_Cursor;
            m_Pending.beforeAnchor = m_Anchor;
            m_Pending.kind = kind;
        }
    }

    void CodeEditor::EndEdit(){
        MarkChanged();
        m_Cursor = Clamp(m_Cursor);
        m_Anchor = Clamp(m_Anchor);
        double now = ImGui::GetTime();
        if (m_MergeEdit){
            UndoRecord& r = m_Undo.back();
            r.after = GetText();
            r.afterCursor = m_Cursor;
            r.afterAnchor = m_Anchor;
            r.time = now;
        }
        else{
            m_Pending.after = GetText();
            if (m_Pending.after != m_Pending.before){
                m_Pending.afterCursor = m_Cursor;
                m_Pending.afterAnchor = m_Anchor;
                m_Pending.time = now;
                m_Undo.resize(m_UndoIndex);
                m_Undo.push_back(std::move(m_Pending));
                if (m_Undo.size() > 500) { m_Undo.erase(m_Undo.begin()); }
                m_UndoIndex = (int)m_Undo.size();
            }
        }
        m_ScrollToCursor = true;
        m_CaretTime = now;
        m_PreferredX = -1.0f;
    }

    void CodeEditor::Undo(){
        if (!CanUndo()) { return; }
        const UndoRecord& r = m_Undo[--m_UndoIndex];
        SetLines(r.before);
        m_Cursor = Clamp(r.beforeCursor);
        m_Anchor = Clamp(r.beforeAnchor);
        MarkChanged();
        m_ScrollToCursor = true;
        m_CompOpen = false;
    }

    void CodeEditor::Redo(){
        if (!CanRedo()) { return; }
        const UndoRecord& r = m_Undo[m_UndoIndex++];
        SetLines(r.after);
        m_Cursor = Clamp(r.afterCursor);
        m_Anchor = Clamp(r.afterAnchor);
        MarkChanged();
        m_ScrollToCursor = true;
        m_CompOpen = false;
    }

    // ===================================================================== editing

    void CodeEditor::InsertText(const std::string& text){
        BeginEdit(EditKind::Other);
        ReplaceSelection(text);
        EndEdit();
    }

    void CodeEditor::TypeChar(unsigned int c){
        if (c < 32 || c == 127) { return; }
        char utf8[5];
        ImTextCharToUtf8(utf8, c);

        if (autoClosePairs && c < 128){
            const char ch = (char)c;
            const std::string& line = m_Lines[m_Cursor.line];
            const char next = m_Cursor.col < (int)line.size() ? line[m_Cursor.col] : 0;
            const char prev = m_Cursor.col > 0 ? line[m_Cursor.col - 1] : 0;
            const bool isQuote = ch == '"' || ch == '\'';
            Tok here = KindAt(m_Cursor);

            // Type over a closer that is already there.
            if (!HasSelection() && next == ch && (std::strchr(")]}", ch) || (isQuote && here == Tok::String))){
                SetCursor({ m_Cursor.line, m_Cursor.col + 1 }, false);
                m_CompOpen = false;
                return;
            }

            const char* open = std::strchr("([{", ch);
            if (open || isQuote){
                const char close = open ? ")]}"[open - "([{"] : ch;
                if (HasSelection()){
                    // Wrap the selection.
                    BeginEdit(EditKind::Other);
                    Coord a = SelStart(), b = SelEnd();
                    std::string sel = GetRange(a, b);
                    DeleteRange(a, b);
                    InsertAt(a, std::string(1, ch) + sel + close);
                    m_Anchor = { a.line, a.col + 1 };
                    m_Cursor = (a.line == b.line) ? Coord{ b.line, b.col + 1 } : b;
                    EndEdit();
                    m_CompOpen = false;
                    return;
                }
                bool nextFree = next == 0 || next == ' ' || std::strchr(")]},;", next);
                bool pair = nextFree && here != Tok::String && here != Tok::Comment;
                if (isQuote && IsIdentChar(prev)) { pair = false; }
                if (pair){
                    BeginEdit(EditKind::Typing);
                    InsertAt(m_Cursor, std::string(1, ch) + close);
                    m_Cursor.col++;
                    m_Anchor = m_Cursor;
                    EndEdit();
                    m_CompOpen = false;
                    return;
                }
            }
        }

        BeginEdit(EditKind::Typing);
        ReplaceSelection(utf8);
        EndEdit();

        if (c < 128 && (std::isalpha((int)c) || c == '}')) { AutoDedent(); }

        if (c < 128 && IsIdentChar((char)c)){
            if (!m_CompOpen) { m_CompExplicit = false; }
            if (autoComplete || m_CompOpen) { UpdateCompletion(m_CompOpen); }
        }
        else if (c == '.' || c == ':'){
            m_CompExplicit = false;
            if (autoComplete) { UpdateCompletion(false); }
        }
        else{
            m_CompOpen = false;
        }
    }

    // Dedent a line that just became `end` / `else` / `elseif` / `until` / `}`.
    void CodeEditor::AutoDedent(){
        std::string& line = m_Lines[m_Cursor.line];
        int indent = LeadingSpaces(line);
        std::string word = line.substr(indent);
        while (!word.empty() && word.back() == ' ') { word.pop_back(); }
        if (m_Cursor.col != indent + (int)word.size()) { return; }
        if (word != "end" && word != "else" && word != "elseif" && word != "until" && word != "}") { return; }

        int p = m_Cursor.line - 1;
        while (p >= 0 && IsBlank(m_Lines[p])) { p--; }
        if (p < 0) { return; }
        int prevIndent = LeadingSpaces(m_Lines[p]);
        int target = LineOpensBlock(m_Lines[p], nullptr) ? prevIndent : std::max(0, prevIndent - tabSize);
        if (target >= indent) { return; }

        BeginEdit(EditKind::Typing); // merges into the word just typed
        m_Lines[m_Cursor.line].erase(0, indent - target);
        m_Cursor.col -= indent - target;
        m_Anchor = m_Cursor;
        EndEdit();
    }

    // Whether code after this line should be indented one level (block or bracket
    // left open). byKeyword: the open block needs an `end` (function / then / do).
    bool CodeEditor::LineOpensBlock(const std::string& text, bool* byKeyword) const{
        std::vector<Token> tokens;
        Tokenize(text, 0, &tokens);
        int net = 0;
        bool keyword = false, leading = true;
        for (const Token& t : tokens){
            if (t.kind == Tok::Comment) { continue; }
            std::string w = text.substr(t.start, t.end - t.start);
            if (t.kind == Tok::Keyword){
                if (leading && (w == "end" || w == "else" || w == "elseif" || w == "until")){
                    // Already dedented by being first on the line; `else` opens again.
                    if (w == "else") { net++; keyword = true; }
                    leading = false;
                    continue;
                }
                leading = false;
                if (w == "function" || w == "do" || w == "then") { net++; keyword = true; }
                else if (w == "repeat") { net++; }
                else if (w == "end" || w == "until" || w == "elseif") { net--; }
            }
            else if (t.kind == Tok::Punct){
                char c = w[0];
                if (leading && (c == '}' || c == ')' || c == ']')) { continue; }
                leading = false;
                if (c == '(' || c == '{' || c == '[') { net++; }
                else if (c == ')' || c == '}' || c == ']') { net--; }
            }
            else{
                leading = false;
            }
        }
        if (byKeyword) { *byKeyword = keyword && net > 0; }
        return net > 0;
    }

    // Blocks opened minus blocks closed over the whole text (`if`/`do`/`function`/
    // `repeat` vs `end`/`until`). Positive means some block is missing its end.
    int CodeEditor::BlockBalance() const{
        int balance = 0, state = 0;
        std::vector<Token> tokens;
        for (const std::string& line : m_Lines){
            tokens.clear();
            state = Tokenize(line, state, &tokens);
            for (const Token& t : tokens){
                if (t.kind != Tok::Keyword) { continue; }
                std::string w = line.substr(t.start, t.end - t.start);
                if (w == "function" || w == "if" || w == "do" || w == "repeat") { balance++; }
                else if (w == "end" || w == "until") { balance--; }
            }
        }
        return balance;
    }

    void CodeEditor::NewLine(){
        BeginEdit(EditKind::Other);
        if (HasSelection()){
            Coord s = SelStart();
            DeleteRange(s, SelEnd());
            m_Cursor = m_Anchor = s;
        }
        const int lineIdx = m_Cursor.line;
        const std::string line = m_Lines[lineIdx];
        std::string before = line.substr(0, m_Cursor.col);
        std::string after = line.substr(m_Cursor.col);
        const int indent = std::min(LeadingSpaces(line), m_Cursor.col);

        bool byKeyword = false;
        bool opens = LineOpensBlock(before, &byKeyword);

        std::size_t a = after.find_first_not_of(' ');
        std::string afterTrim = (a == std::string::npos) ? "" : after.substr(a);
        while (!before.empty() && before.back() == ' ' && (int)before.size() > indent) { before.pop_back(); }
        if (IsBlank(before)) { before.clear(); }

        const std::string pad(indent, ' ');
        const std::string inner(indent + (opens ? tabSize : 0), ' ');

        char lastBefore = 0;
        for (int i = (int)before.size() - 1; i >= 0; i--) { if (before[i] != ' ') { lastBefore = before[i]; break; } }
        char firstAfter = afterTrim.empty() ? 0 : afterTrim[0];
        bool betweenPair = (lastBefore == '(' && firstAfter == ')') || (lastBefore == '[' && firstAfter == ']')
                        || (lastBefore == '{' && firstAfter == '}');
        auto startsWithWord = [&](const char* w){
            std::size_t n = std::strlen(w);
            return afterTrim.compare(0, n, w) == 0 && (afterTrim.size() == n || !IsIdentChar(afterTrim[n]));
        };
        bool closesAfter = opens && (startsWithWord("end") || startsWithWord("until") || startsWithWord("else") || startsWithWord("elseif"));

        m_Lines[lineIdx] = before;
        if (opens && (betweenPair || closesAfter)){
            // Split "(|)" / "then|end" into three lines with the cursor in the middle.
            m_Lines.insert(m_Lines.begin() + lineIdx + 1, inner);
            m_Lines.insert(m_Lines.begin() + lineIdx + 2, pad + afterTrim);
            m_Cursor = { lineIdx + 1, (int)inner.size() };
        }
        else{
            m_Lines.insert(m_Lines.begin() + lineIdx + 1, inner + afterTrim);
            m_Cursor = { lineIdx + 1, (int)inner.size() };
            if (autoCloseBlocks && byKeyword && afterTrim.empty() && BlockBalance() > 0)
                m_Lines.insert(m_Lines.begin() + lineIdx + 2, pad + "end");
        }
        m_Anchor = m_Cursor;
        EndEdit();
    }

    void CodeEditor::Backspace(bool word){
        if (HasSelection()){
            BeginEdit(EditKind::Other);
            ReplaceSelection("");
            EndEdit();
            return;
        }
        Coord c = m_Cursor;
        if (c.line == 0 && c.col == 0) { return; }
        BeginEdit(EditKind::Deleting);
        const std::string line = m_Lines[c.line];
        if (c.col == 0){
            Coord p{ c.line - 1, (int)m_Lines[c.line - 1].size() };
            DeleteRange(p, c);
            m_Cursor = p;
        }
        else if (word){
            Coord s = MoveWordLeft(c);
            DeleteRange(s, c);
            m_Cursor = s;
        }
        else if (LeadingSpaces(line) >= c.col){
            // In the indentation: remove back to the previous tab stop.
            int to = ((c.col - 1) / tabSize) * tabSize;
            DeleteRange({ c.line, to }, c);
            m_Cursor.col = to;
        }
        else{
            int p = PrevCharCol(c.line, c.col);
            char pc = line[p], nc = c.col < (int)line.size() ? line[c.col] : 0;
            bool pair = autoClosePairs && ((pc == '(' && nc == ')') || (pc == '[' && nc == ']') || (pc == '{' && nc == '}')
                                        || (pc == '"' && nc == '"') || (pc == '\'' && nc == '\''));
            DeleteRange({ c.line, p }, pair ? Coord{ c.line, c.col + 1 } : c);
            m_Cursor.col = p;
        }
        m_Anchor = m_Cursor;
        EndEdit();
        if (m_CompOpen) { UpdateCompletion(true); }
    }

    void CodeEditor::DeleteForward(bool word){
        if (HasSelection()){
            BeginEdit(EditKind::Other);
            ReplaceSelection("");
            EndEdit();
            return;
        }
        Coord c = m_Cursor;
        const std::string& line = m_Lines[c.line];
        if (c.col >= (int)line.size() && c.line + 1 >= (int)m_Lines.size()) { return; }
        BeginEdit(EditKind::Deleting);
        if (c.col >= (int)line.size()) { DeleteRange(c, { c.line + 1, 0 }); }
        else if (word) { DeleteRange(c, MoveWordRight(c)); }
        else { DeleteRange(c, { c.line, NextCharCol(c.line, c.col) }); }
        m_Anchor = m_Cursor;
        EndEdit();
        m_CompOpen = false;
    }

    void CodeEditor::IndentLines(bool unindent){
        Coord s = SelStart(), e = SelEnd();
        int l0 = s.line, l1 = e.line;
        if (e.col == 0 && l1 > l0) { l1--; }
        BeginEdit(EditKind::Other);
        for (int l = l0; l <= l1; l++){
            std::string& line = m_Lines[l];
            int delta = 0;
            if (unindent){
                int lead = LeadingSpaces(line);
                int remove = std::min(lead, lead % tabSize == 0 ? tabSize : lead % tabSize);
                line.erase(0, remove);
                delta = -remove;
            }
            else{
                if (line.empty() && l0 != l1) { continue; }
                line.insert(0, std::string(tabSize, ' '));
                delta = tabSize;
            }
            for (Coord* c : { &m_Cursor, &m_Anchor }){
                if (c->line != l) { continue; }
                if (delta > 0 && c->col == 0 && HasSelection() && *c == SelStart() && l0 != l1) { continue; }
                c->col = std::max(0, c->col + delta);
            }
        }
        EndEdit();
    }

    void CodeEditor::MoveLines(int dir){
        int l0 = SelStart().line, l1 = SelEnd().line;
        if (HasSelection() && SelEnd().col == 0 && l1 > l0) { l1--; }
        if (dir < 0 && l0 == 0) { return; }
        if (dir > 0 && l1 + 1 >= (int)m_Lines.size()) { return; }
        BeginEdit(EditKind::Other);
        if (dir < 0){
            std::string moved = m_Lines[l0 - 1];
            m_Lines.erase(m_Lines.begin() + l0 - 1);
            m_Lines.insert(m_Lines.begin() + l1, moved);
        }
        else{
            std::string moved = m_Lines[l1 + 1];
            m_Lines.erase(m_Lines.begin() + l1 + 1);
            m_Lines.insert(m_Lines.begin() + l0, moved);
        }
        m_Cursor.line += dir;
        m_Anchor.line += dir;
        EndEdit();
    }

    void CodeEditor::DuplicateSelection(){
        BeginEdit(EditKind::Other);
        if (HasSelection()){
            Coord e = SelEnd();
            std::string text = GetRange(SelStart(), e);
            Coord end = InsertAt(e, text);
            m_Anchor = e;
            m_Cursor = end;
        }
        else{
            std::string copy = m_Lines[m_Cursor.line];
            m_Lines.insert(m_Lines.begin() + m_Cursor.line + 1, copy);
            m_Cursor.line++;
            m_Anchor = m_Cursor;
        }
        EndEdit();
    }

    void CodeEditor::DeleteLines(){
        int l0 = SelStart().line, l1 = SelEnd().line;
        if (HasSelection() && SelEnd().col == 0 && l1 > l0) { l1--; }
        BeginEdit(EditKind::Other);
        m_Lines.erase(m_Lines.begin() + l0, m_Lines.begin() + l1 + 1);
        if (m_Lines.empty()) { m_Lines.emplace_back(); }
        m_Cursor = m_Anchor = Clamp({ l0, m_Cursor.col });
        EndEdit();
    }

    void CodeEditor::ToggleComment(){
        int l0 = SelStart().line, l1 = SelEnd().line;
        if (HasSelection() && SelEnd().col == 0 && l1 > l0) { l1--; }
        bool allCommented = true, any = false;
        int minIndent = INT_MAX;
        for (int l = l0; l <= l1; l++){
            const std::string& s = m_Lines[l];
            if (IsBlank(s)) { continue; }
            any = true;
            int ind = LeadingSpaces(s);
            minIndent = std::min(minIndent, ind);
            if (s.compare(ind, 2, "--") != 0) { allCommented = false; }
        }
        if (!any) { return; }
        BeginEdit(EditKind::Other);
        for (int l = l0; l <= l1; l++){
            std::string& s = m_Lines[l];
            if (IsBlank(s)) { continue; }
            if (allCommented){
                int ind = LeadingSpaces(s);
                int remove = 2 + ((int)s.size() > ind + 2 && s[ind + 2] == ' ' ? 1 : 0);
                s.erase(ind, remove);
                for (Coord* c : { &m_Cursor, &m_Anchor })
                    if (c->line == l && c->col > ind) { c->col = std::max(ind, c->col - remove); }
            }
            else{
                s.insert(minIndent, "-- ");
                for (Coord* c : { &m_Cursor, &m_Anchor })
                    if (c->line == l && c->col >= minIndent) { c->col += 3; }
            }
        }
        EndEdit();
    }

    void CodeEditor::Copy(){
        std::string text = HasSelection() ? GetRange(SelStart(), SelEnd()) : m_Lines[m_Cursor.line] + "\n";
        ImGui::SetClipboardText(text.c_str());
    }

    void CodeEditor::Cut(){
        Copy();
        if (HasSelection()){
            BeginEdit(EditKind::Other);
            ReplaceSelection("");
            EndEdit();
        }
        else{
            DeleteLines();
        }
    }

    void CodeEditor::Paste(){
        const char* clip = ImGui::GetClipboardText();
        if (!clip || !*clip) { return; }
        BeginEdit(EditKind::Other);
        ReplaceSelection(clip);
        EndEdit();
    }

    void CodeEditor::SelectAll(){
        m_Anchor = { 0, 0 };
        m_Cursor = { (int)m_Lines.size() - 1, (int)m_Lines.back().size() };
        m_CaretTime = ImGui::GetTime();
    }

    void CodeEditor::GoToLine(int line){
        int l = std::max(0, std::min(line - 1, (int)m_Lines.size() - 1));
        m_Cursor = m_Anchor = { l, LeadingSpaces(m_Lines[l]) };
        m_ScrollToCursor = true;
        m_CenterCursor = true;
        m_WantFocus = true;
        m_CompOpen = false;
    }

    // ===================================================================== syntax

    void CodeEditor::SetApi(const std::vector<ApiItem>& api){
        m_Api = api;
        m_ApiIndex.clear();
        m_ApiNames.clear();
        const auto& builtins = GetBuiltins();
        for (int i = 0; i < (int)builtins.size(); i++) { m_ApiIndex[builtins[i].name] = -(i + 1); }
        for (int i = 0; i < (int)m_Api.size(); i++){
            m_ApiIndex[m_Api[i].name] = i;
            const std::string& n = m_Api[i].name;
            m_ApiNames.insert(n.substr(0, n.find('.'))); // "Key.Space" highlights "Key"
        }
    }

    const CodeEditor::ApiItem* CodeEditor::LookupApi(const std::string& name) const{
        auto it = m_ApiIndex.find(name);
        if (it == m_ApiIndex.end()) { return nullptr; }
        return it->second >= 0 ? &m_Api[it->second] : &GetBuiltins()[-it->second - 1];
    }

    const std::vector<CodeEditor::ApiItem>& CodeEditor::GetBuiltins() const{
        using K = ItemKind;
        static const std::vector<ApiItem> items = {
            { "assert", "assert(v [, message])", "Raises an error if v is false or nil.", K::Function },
            { "error", "error(message [, level])", "Raises an error.", K::Function },
            { "ipairs", "ipairs(t)", "Iterates t[1], t[2], ...: for i, v in ipairs(t) do ... end", K::Function },
            { "pairs", "pairs(t)", "Iterates every key: for k, v in pairs(t) do ... end", K::Function },
            { "next", "next(t [, key]) -> key, value", "The pair after key in t.", K::Function },
            { "pcall", "pcall(f, ...) -> ok, ...", "Calls f, returning false and the error instead of raising it.", K::Function },
            { "xpcall", "xpcall(f, handler, ...) -> ok, ...", "Like pcall, with a message handler.", K::Function },
            { "select", "select(n, ...)", "Arguments after the n-th; select('#', ...) counts them.", K::Function },
            { "tonumber", "tonumber(v [, base]) -> number", "Converts to a number, or nil.", K::Function },
            { "tostring", "tostring(v) -> string", "Converts any value to a string.", K::Function },
            { "type", "type(v) -> string", "\"nil\", \"number\", \"string\", \"boolean\", \"table\", \"function\"...", K::Function },
            { "setmetatable", "setmetatable(t, mt) -> t", "Sets the metatable of t.", K::Function },
            { "getmetatable", "getmetatable(t) -> mt", "The metatable of t.", K::Function },
            { "rawget", "rawget(t, k)", "t[k] without metamethods.", K::Function },
            { "rawset", "rawset(t, k, v)", "t[k] = v without metamethods.", K::Function },
            { "rawequal", "rawequal(a, b) -> bool", "a == b without metamethods.", K::Function },
            { "rawlen", "rawlen(v) -> n", "Length without metamethods.", K::Function },
            { "_G", "_G", "The global environment table.", K::Module },

            { "math", "math", "Mathematical functions.", K::Module },
            { "math.abs", "math.abs(x)", "Absolute value.", K::Function },
            { "math.ceil", "math.ceil(x)", "Smallest integer >= x.", K::Function },
            { "math.floor", "math.floor(x)", "Largest integer <= x.", K::Function },
            { "math.sqrt", "math.sqrt(x)", "Square root.", K::Function },
            { "math.sin", "math.sin(x)", "Sine (radians).", K::Function },
            { "math.cos", "math.cos(x)", "Cosine (radians).", K::Function },
            { "math.tan", "math.tan(x)", "Tangent (radians).", K::Function },
            { "math.asin", "math.asin(x)", "Arc sine (radians).", K::Function },
            { "math.acos", "math.acos(x)", "Arc cosine (radians).", K::Function },
            { "math.atan", "math.atan(y [, x])", "Arc tangent of y/x using both signs (radians).", K::Function },
            { "math.exp", "math.exp(x)", "e raised to x.", K::Function },
            { "math.log", "math.log(x [, base])", "Logarithm (natural by default).", K::Function },
            { "math.max", "math.max(x, ...)", "Largest argument.", K::Function },
            { "math.min", "math.min(x, ...)", "Smallest argument.", K::Function },
            { "math.fmod", "math.fmod(x, y)", "Remainder of x / y, rounded towards zero.", K::Function },
            { "math.modf", "math.modf(x) -> int, frac", "Integral and fractional parts.", K::Function },
            { "math.random", "math.random([m [, n]])", "Float in [0,1), or integer in [1,m] / [m,n].", K::Function },
            { "math.randomseed", "math.randomseed([x])", "Seeds the random generator.", K::Function },
            { "math.rad", "math.rad(degrees)", "Degrees to radians.", K::Function },
            { "math.deg", "math.deg(radians)", "Radians to degrees.", K::Function },
            { "math.tointeger", "math.tointeger(x)", "x as an integer, or nil.", K::Function },
            { "math.type", "math.type(x)", "\"integer\", \"float\" or nil.", K::Function },
            { "math.pi", "math.pi", "3.14159...", K::Constant },
            { "math.huge", "math.huge", "Infinity.", K::Constant },
            { "math.maxinteger", "math.maxinteger", "Largest integer.", K::Constant },
            { "math.mininteger", "math.mininteger", "Smallest integer.", K::Constant },

            { "string", "string", "String functions (also callable as s:upper() etc.).", K::Module },
            { "string.format", "string.format(fmt, ...)", "printf-style formatting: string.format(\"%.2f\", x).", K::Function },
            { "string.len", "string.len(s)", "Length in bytes.", K::Function },
            { "string.sub", "string.sub(s, i [, j])", "Substring from i to j (1-based, negative from the end).", K::Function },
            { "string.upper", "string.upper(s)", "Upper-case copy.", K::Function },
            { "string.lower", "string.lower(s)", "Lower-case copy.", K::Function },
            { "string.rep", "string.rep(s, n [, sep])", "s repeated n times.", K::Function },
            { "string.reverse", "string.reverse(s)", "Reversed copy.", K::Function },
            { "string.find", "string.find(s, pattern [, init [, plain]])", "Start and end of the first match, or nil.", K::Function },
            { "string.match", "string.match(s, pattern [, init])", "Captures of the first match, or nil.", K::Function },
            { "string.gmatch", "string.gmatch(s, pattern)", "Iterator over all matches.", K::Function },
            { "string.gsub", "string.gsub(s, pattern, repl [, n])", "Copy with matches replaced, and the count.", K::Function },
            { "string.byte", "string.byte(s [, i [, j]])", "Byte values.", K::Function },
            { "string.char", "string.char(...)", "String from byte values.", K::Function },

            { "table", "table", "Table (array) functions.", K::Module },
            { "table.insert", "table.insert(t, [pos,] value)", "Inserts value at pos (default: the end).", K::Function },
            { "table.remove", "table.remove(t [, pos])", "Removes and returns t[pos] (default: the last).", K::Function },
            { "table.concat", "table.concat(t [, sep [, i [, j]]])", "Joins array elements into a string.", K::Function },
            { "table.sort", "table.sort(t [, comp])", "Sorts in place.", K::Function },
            { "table.unpack", "table.unpack(t [, i [, j]])", "Returns the elements as values.", K::Function },
            { "table.pack", "table.pack(...)", "Table of the arguments, with field n.", K::Function },
            { "table.move", "table.move(a1, f, e, t [, a2])", "Copies a1[f..e] to a2[t..].", K::Function },

            { "coroutine", "coroutine", "Coroutines.", K::Module },
            { "coroutine.create", "coroutine.create(f)", "New coroutine running f.", K::Function },
            { "coroutine.resume", "coroutine.resume(co, ...)", "Starts or continues co.", K::Function },
            { "coroutine.yield", "coroutine.yield(...)", "Suspends the running coroutine.", K::Function },
            { "coroutine.status", "coroutine.status(co)", "\"running\", \"suspended\", \"normal\" or \"dead\".", K::Function },
            { "coroutine.wrap", "coroutine.wrap(f)", "Function that resumes a new coroutine each call.", K::Function },
            { "coroutine.running", "coroutine.running()", "The running coroutine.", K::Function },
            { "coroutine.isyieldable", "coroutine.isyieldable()", "Whether the running code can yield.", K::Function },

            { "utf8", "utf8", "UTF-8 helpers.", K::Module },
            { "utf8.char", "utf8.char(...)", "String from code points.", K::Function },
            { "utf8.len", "utf8.len(s [, i [, j]])", "Number of characters.", K::Function },
            { "utf8.codepoint", "utf8.codepoint(s [, i [, j]])", "Code points.", K::Function },
            { "utf8.offset", "utf8.offset(s, n [, i])", "Byte position of the n-th character.", K::Function },
            { "utf8.codes", "utf8.codes(s)", "Iterator over (position, code point).", K::Function },
            { "utf8.charpattern", "utf8.charpattern", "Pattern matching one UTF-8 character.", K::Constant },
        };
        return items;
    }

    // Lexes one line. `state` is the lexer state at the line start: 0 normal,
    // n > 0 inside a long comment of level n-1, n < 0 inside a long string of level
    // -n-1. Returns the state at the end of the line.
    int CodeEditor::Tokenize(const std::string& L, int state, std::vector<Token>* out) const{
        static std::unordered_set<std::string> builtinNames = [this]{
            std::unordered_set<std::string> names;
            for (const ApiItem& b : GetBuiltins())
                if (b.name.find('.') == std::string::npos) { names.insert(b.name); }
            return names;
        }();
        const std::size_t n = L.size();
        std::size_t i = 0;
        auto push = [&](std::size_t a, std::size_t b, Tok k){
            if (out && b > a) { out->push_back({ (int)a, (int)b, k }); }
        };

        if (state != 0){
            int level = state > 0 ? state - 1 : -state - 1;
            Tok k = state > 0 ? Tok::Comment : Tok::String;
            std::size_t close = FindLongClose(L, 0, level);
            if (close == std::string::npos) { push(0, n, k); return state; }
            push(0, close, k);
            i = close;
        }

        std::string prevWord;
        while (i < n){
            const char c = L[i];
            if (c == '-' && i + 1 < n && L[i + 1] == '-'){
                int level = LongBracketLevel(L, i + 2);
                if (level >= 0){
                    std::size_t close = FindLongClose(L, i + 2 + level + 2, level);
                    if (close == std::string::npos) { push(i, n, Tok::Comment); return level + 1; }
                    push(i, close, Tok::Comment);
                    i = close;
                    continue;
                }
                push(i, n, Tok::Comment);
                break;
            }
            if (c == '['){
                int level = LongBracketLevel(L, i);
                if (level >= 0){
                    std::size_t close = FindLongClose(L, i + level + 2, level);
                    if (close == std::string::npos) { push(i, n, Tok::String); return -(level + 1); }
                    push(i, close, Tok::String);
                    i = close;
                    continue;
                }
            }
            if (c == '"' || c == '\''){
                std::size_t j = i + 1;
                while (j < n && L[j] != c) { j += (L[j] == '\\') ? 2 : 1; }
                j = std::min(j + 1, n);
                push(i, j, Tok::String);
                i = j;
                continue;
            }
            if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)L[i + 1]))){
                std::size_t j = i;
                if (c == '0' && i + 1 < n && (L[i + 1] == 'x' || L[i + 1] == 'X')){
                    j += 2;
                    while (j < n && (std::isxdigit((unsigned char)L[j]) || L[j] == '.')) { j++; }
                }
                else{
                    while (j < n && (std::isalnum((unsigned char)L[j]) || L[j] == '.'
                                  || ((L[j] == '+' || L[j] == '-') && (L[j - 1] == 'e' || L[j - 1] == 'E')))) { j++; }
                }
                push(i, j, Tok::Number);
                i = j;
                continue;
            }
            if (IsIdentStart(c)){
                std::size_t j = i;
                while (j < n && IsIdentChar(L[j])) { j++; }
                std::string w = L.substr(i, j - i);
                Tok k = Tok::Default;
                if (LuaKeywords().count(w)) { k = (w == "true" || w == "false" || w == "nil") ? Tok::Constant : Tok::Keyword; }
                else if (w == "self") { k = Tok::Self; }
                else if (prevWord == "function") { k = Tok::Function; }
                else if (i > 0 && L[i - 1] == '.') {
                    std::size_t p = j;
                    while (p < n && L[p] == ' ') { p++; }
                    k = (p < n && L[p] == '(') ? Tok::Function : Tok::Default;
                }
                else if (m_ApiNames.count(w)) { k = Tok::Api; }
                else if (builtinNames.count(w)) { k = Tok::Builtin; }
                else{
                    std::size_t p = j;
                    while (p < n && L[p] == ' ') { p++; }
                    if (p < n && (L[p] == '(' || L[p] == '"' || L[p] == '{')) { k = Tok::Function; }
                }
                push(i, j, k);
                prevWord = w;
                i = j;
                continue;
            }
            if (c != ' '){
                push(i, i + 1, Tok::Punct);
                if (c != '.' && c != ':') { prevWord.clear(); }
            }
            i++;
        }
        return 0;
    }

    void CodeEditor::UpdateLineStates(){
        if (m_StatesVersion == m_Version && m_LineStates.size() == m_Lines.size()) { return; }
        m_LineStates.resize(m_Lines.size());
        int state = 0;
        m_LongestLine = 0;
        for (std::size_t i = 0; i < m_Lines.size(); i++){
            m_LineStates[i] = state;
            state = Tokenize(m_Lines[i], state, nullptr);
            m_LongestLine = std::max(m_LongestLine, Utf8Length(m_Lines[i]));
        }
        m_StatesVersion = m_Version;
    }

    // Token kind the position is inside (strings/comments count up to the end of an
    // unterminated one).
    CodeEditor::Tok CodeEditor::KindAt(Coord c) const{
        if (m_StatesVersion != m_Version) { const_cast<CodeEditor*>(this)->UpdateLineStates(); }
        const std::string& L = m_Lines[c.line];
        std::vector<Token> tokens;
        Tokenize(L, m_LineStates[c.line], &tokens);
        for (const Token& t : tokens){
            if (t.start < c.col && c.col < t.end) { return t.kind; }
            if (c.col == t.end && t.end == (int)L.size() && t.start < c.col){
                if (t.kind == Tok::Comment) { return t.kind; }
                if (t.kind == Tok::String){
                    char q = L[t.start];
                    bool closed = (q == '"' || q == '\'') && t.end - t.start >= 2 && L[t.end - 1] == q
                                  && L[t.end - 2] != '\\';
                    if (q == '[') { closed = L[t.end - 1] == ']'; }
                    if (!closed) { return t.kind; }
                }
            }
        }
        return Tok::Default;
    }

    bool CodeEditor::FindMatchingBracket(Coord at, Coord& outOpen, Coord& outClose) const{
        static const char* opens = "([{";
        static const char* closes = ")]}";
        std::vector<Token> tokens;
        auto punctAt = [&](int line, int col) -> char{
            const std::string& L = m_Lines[line];
            if (col < 0 || col >= (int)L.size() || !std::strchr("()[]{}", L[col])) { return 0; }
            tokens.clear();
            Tokenize(L, m_LineStates[line], &tokens);
            for (const Token& t : tokens) { if (t.start == col) { return t.kind == Tok::Punct ? L[col] : 0; } }
            return 0;
        };
        Coord p = at;
        char ch = punctAt(at.line, at.col);
        if (!ch) { p.col = at.col - 1; ch = punctAt(at.line, p.col); }
        if (!ch) { return false; }

        const bool forward = std::strchr(opens, ch) != nullptr;
        const char open = forward ? ch : opens[std::strchr(closes, ch) - closes];
        const char close = forward ? closes[std::strchr(opens, ch) - opens] : ch;
        int depth = 0, scanned = 0;
        for (int l = p.line; l >= 0 && l < (int)m_Lines.size() && scanned++ < 3000; l += forward ? 1 : -1){
            const std::string& L = m_Lines[l];
            tokens.clear();
            Tokenize(L, m_LineStates[l], &tokens);
            if (!forward) { std::reverse(tokens.begin(), tokens.end()); }
            for (const Token& t : tokens){
                if (t.kind != Tok::Punct) { continue; }
                if (l == p.line && (forward ? t.start < p.col : t.start > p.col)) { continue; }
                char k = L[t.start];
                if (k == (forward ? open : close)) { depth++; }
                else if (k == (forward ? close : open)){
                    if (--depth == 0){
                        outOpen = forward ? p : Coord{ l, t.start };
                        outClose = forward ? Coord{ l, t.start } : p;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // ===================================================================== find

    void CodeEditor::UpdateMatches(){
        int flags = (m_MatchCase ? 1 : 0) | (m_WholeWord ? 2 : 0) | (m_UseRegex ? 4 : 0);
        if (m_MatchesVersion == m_Version && m_MatchesQuery == m_FindBuf && m_MatchesFlags == flags) { return; }
        m_MatchesVersion = m_Version;
        m_MatchesQuery = m_FindBuf;
        m_MatchesFlags = flags;
        m_Matches.clear();
        m_RegexError = false;
        const std::string query = m_FindBuf;
        if (query.empty()) { return; }

        auto wholeWordOk = [&](const std::string& L, std::size_t s, std::size_t e){
            if (!m_WholeWord) { return true; }
            return (s == 0 || !IsIdentChar(L[s - 1])) && (e >= L.size() || !IsIdentChar(L[e]));
        };
        if (m_UseRegex){
            try{
                auto f = std::regex::ECMAScript | (m_MatchCase ? std::regex::flag_type(0) : std::regex::icase);
                std::regex re(query, f);
                for (int l = 0; l < (int)m_Lines.size() && m_Matches.size() < 10000; l++){
                    const std::string& L = m_Lines[l];
                    for (auto it = std::sregex_iterator(L.begin(), L.end(), re); it != std::sregex_iterator(); ++it){
                        if (it->length() == 0) { continue; }
                        std::size_t s = (std::size_t)it->position(), e = s + (std::size_t)it->length();
                        if (wholeWordOk(L, s, e)) { m_Matches.push_back({ { l, (int)s }, { l, (int)e } }); }
                    }
                }
            }
            catch (const std::regex_error&){
                m_RegexError = true;
                m_Matches.clear();
            }
            return;
        }
        const std::string q = m_MatchCase ? query : ToLower(query);
        for (int l = 0; l < (int)m_Lines.size() && m_Matches.size() < 10000; l++){
            const std::string L = m_MatchCase ? m_Lines[l] : ToLower(m_Lines[l]);
            for (std::size_t pos = L.find(q); pos != std::string::npos; pos = L.find(q, pos + q.size())){
                if (wholeWordOk(L, pos, pos + q.size()))
                    m_Matches.push_back({ { l, (int)pos }, { l, (int)(pos + q.size()) } });
            }
        }
    }

    void CodeEditor::FindNext(bool forward){
        UpdateMatches();
        if (m_Matches.empty()) { return; }
        int idx = -1;
        if (forward){
            Coord from = SelEnd();
            for (int i = 0; i < (int)m_Matches.size(); i++) { if (m_Matches[i].start >= from) { idx = i; break; } }
            if (idx < 0) { idx = 0; }
        }
        else{
            Coord from = SelStart();
            for (int i = (int)m_Matches.size() - 1; i >= 0; i--) { if (m_Matches[i].start < from) { idx = i; break; } }
            if (idx < 0) { idx = (int)m_Matches.size() - 1; }
        }
        m_Anchor = m_Matches[idx].start;
        m_Cursor = m_Matches[idx].end;
        m_ScrollToCursor = true;
        m_CenterCursor = true;
    }

    void CodeEditor::ReplaceCurrent(){
        UpdateMatches();
        for (const Match& m : m_Matches){
            if (m.start != SelStart() || m.end != SelEnd()) { continue; }
            std::string replacement = m_ReplaceBuf;
            if (m_UseRegex){
                try{
                    auto f = std::regex::ECMAScript | (m_MatchCase ? std::regex::flag_type(0) : std::regex::icase);
                    replacement = std::regex_replace(GetRange(m.start, m.end), std::regex(m_FindBuf, f), m_ReplaceBuf,
                                                     std::regex_constants::format_first_only);
                }
                catch (const std::regex_error&) { return; }
            }
            BeginEdit(EditKind::Other);
            ReplaceSelection(replacement);
            EndEdit();
            break;
        }
        FindNext(true);
    }

    void CodeEditor::ReplaceAll(){
        UpdateMatches();
        if (m_Matches.empty()) { return; }
        std::regex re;
        if (m_UseRegex){
            try{
                auto f = std::regex::ECMAScript | (m_MatchCase ? std::regex::flag_type(0) : std::regex::icase);
                re = std::regex(m_FindBuf, f);
            }
            catch (const std::regex_error&) { return; }
        }
        std::vector<Match> matches = m_Matches;
        BeginEdit(EditKind::Other);
        for (auto it = matches.rbegin(); it != matches.rend(); ++it){
            std::string replacement = m_ReplaceBuf;
            if (m_UseRegex)
                replacement = std::regex_replace(GetRange(it->start, it->end), re, m_ReplaceBuf,
                                                 std::regex_constants::format_first_only);
            DeleteRange(it->start, it->end);
            InsertAt(it->start, replacement);
        }
        m_Cursor = m_Anchor = Clamp(m_Cursor);
        EndEdit();
    }

    void CodeEditor::OpenFind(bool withReplace){
        m_FindOpen = true;
        m_ReplaceOpen = withReplace;
        m_GoToOpen = false;
        m_FocusFind = true;
        std::string seed;
        if (HasSelection() && SelStart().line == SelEnd().line) { seed = GetRange(SelStart(), SelEnd()); }
        else if (!HasSelection()) { seed = IdentifierAt(m_Cursor); }
        if (!seed.empty()) { std::snprintf(m_FindBuf, sizeof(m_FindBuf), "%s", seed.c_str()); }
    }

    void CodeEditor::OpenGoToLine(){
        m_GoToOpen = true;
        m_FindOpen = false;
        m_FocusGoTo = true;
        std::snprintf(m_GoToBuf, sizeof(m_GoToBuf), "%d", m_Cursor.line + 1);
    }

    void CodeEditor::DrawFindBar(){
        ImGuiIO& io = ImGui::GetIO();
        const ImVec4 activeCol = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
        auto toggle = [&](const char* label, bool* v, const char* tip){
            ImGui::SameLine();
            if (*v) { ImGui::PushStyleColor(ImGuiCol_Button, activeCol); }
            if (ImGui::SmallButton(label)) { *v = !*v; }
            if (*v) { ImGui::PopStyleColor(); }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tip); }
        };

        bool anyActive = false;
        ImGui::SetNextItemWidth(220.0f);
        if (m_FocusFind) { ImGui::SetKeyboardFocusHere(); m_FocusFind = false; }
        if (ImGui::InputTextWithHint("##find", "Find", m_FindBuf, sizeof(m_FindBuf), ImGuiInputTextFlags_EnterReturnsTrue)){
            FindNext(!io.KeyShift);
            m_FocusFind = true; // Enter deactivates the field; keep typing in it
        }
        anyActive |= ImGui::IsItemActive();
        toggle("Aa", &m_MatchCase, "Match case");
        toggle("W", &m_WholeWord, "Whole word");
        toggle(".*", &m_UseRegex, "Regular expression");

        UpdateMatches();
        ImGui::SameLine();
        if (m_RegexError) { ImGui::TextColored(ImVec4(0.94f, 0.4f, 0.4f, 1.0f), "Bad pattern"); }
        else if (m_FindBuf[0] && m_Matches.empty()) { ImGui::TextColored(ImVec4(0.94f, 0.6f, 0.4f, 1.0f), "No results"); }
        else if (m_FindBuf[0]){
            int current = 0;
            for (int i = 0; i < (int)m_Matches.size(); i++)
                if (m_Matches[i].start == SelStart() && m_Matches[i].end == SelEnd()) { current = i + 1; break; }
            if (current) { ImGui::TextDisabled("%d of %d", current, (int)m_Matches.size()); }
            else { ImGui::TextDisabled("%d found", (int)m_Matches.size()); }
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##prev", ImGuiDir_Up)) { FindNext(false); }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Previous (Shift+F3)"); }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##next", ImGuiDir_Down)) { FindNext(true); }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Next (F3 / Enter)"); }
        ImGui::SameLine();
        if (ImGui::SmallButton(m_ReplaceOpen ? "Replace <" : "Replace >")) { m_ReplaceOpen = !m_ReplaceOpen; }
        ImGui::SameLine();
        bool close = ImGui::SmallButton("x");

        if (m_ReplaceOpen){
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputTextWithHint("##replace", "Replace", m_ReplaceBuf, sizeof(m_ReplaceBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                ReplaceCurrent();
            anyActive |= ImGui::IsItemActive();
            ImGui::SameLine();
            if (ImGui::SmallButton("Replace")) { ReplaceCurrent(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Replace All")) { ReplaceAll(); }
        }

        if (close || (m_FindWasActive && ImGui::IsKeyPressed(ImGuiKey_Escape))){
            m_FindOpen = false;
            m_WantFocus = true;
        }
        m_FindWasActive = anyActive;
    }

    void CodeEditor::DrawGoToBar(){
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Go to line");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (m_FocusGoTo) { ImGui::SetKeyboardFocusHere(); m_FocusGoTo = false; }
        bool go = ImGui::InputText("##goto", m_GoToBuf, sizeof(m_GoToBuf),
            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        bool active = ImGui::IsItemActive();
        ImGui::SameLine();
        ImGui::TextDisabled("(1 - %d)", (int)m_Lines.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Go")) { go = true; }
        ImGui::SameLine();
        bool close = ImGui::SmallButton("x##closegoto");
        if (go) { GoToLine(std::atoi(m_GoToBuf)); m_GoToOpen = false; }
        if (close || (m_FindWasActive && ImGui::IsKeyPressed(ImGuiKey_Escape))){
            m_GoToOpen = false;
            m_WantFocus = true;
        }
        m_FindWasActive = active;
    }

    // ===================================================================== completion

    std::string CodeEditor::IdentifierAt(Coord c, Coord* outStart, Coord* outEnd) const{
        const std::string& L = m_Lines[c.line];
        bool onWord = (c.col < (int)L.size() && IsIdentChar(L[c.col])) || (c.col > 0 && IsIdentChar(L[c.col - 1]));
        if (!onWord) { return ""; }
        Coord s = WordStart(c), e = WordEnd(c);
        // Extend left over "a.b.c" so library calls resolve as "math.sin".
        while (s.col > 1 && (L[s.col - 1] == '.' || L[s.col - 1] == ':') && IsIdentChar(L[s.col - 2]))
            s = WordStart({ s.line, s.col - 1 });
        if (outStart) { *outStart = s; }
        if (outEnd) { *outEnd = e; }
        return L.substr(s.col, e.col - s.col);
    }

    void CodeEditor::UpdateCompletion(bool force){
        m_CompOpen = false;
        if (HasSelection()) { return; }
        Tok here = KindAt(m_Cursor);
        if (here == Tok::String || here == Tok::Comment) { return; }

        const std::string& L = m_Lines[m_Cursor.line];
        Coord ws = WordStart(m_Cursor);
        std::string prefix = L.substr(ws.col, m_Cursor.col - ws.col);
        if (!prefix.empty() && std::isdigit((unsigned char)prefix[0])) { return; }

        std::string object;
        if (ws.col > 0 && (L[ws.col - 1] == '.' || L[ws.col - 1] == ':')){
            Coord os = WordStart({ ws.line, ws.col - 1 });
            object = L.substr(os.col, ws.col - 1 - os.col);
            if (object.empty()) { return; }
        }
        if (object.empty() && !force && prefix.size() < 2) { return; }

        std::vector<CompletionEntry> items;
        std::unordered_set<std::string> seen;
        auto consider = [&](const std::string& label, const std::string& insert, const std::string& sig,
                            const std::string& doc, ItemKind kind){
            if (!seen.insert(label).second) { return; }
            int score = Score(label, prefix);
            if (score < 0 || (score < 500 && !m_CompExplicit)) { return; } // fuzzy only on Ctrl+Space
            items.push_back({ label, insert, sig, doc, kind, score });
        };

        auto addAll = [&](const std::vector<ApiItem>& list){
            for (const ApiItem& a : list){
                if (!object.empty()){
                    std::string pre = object + ".";
                    if (a.name.size() <= pre.size() || a.name.compare(0, pre.size(), pre) != 0) { continue; }
                    std::string member = a.name.substr(pre.size());
                    if (member.find('.') != std::string::npos) { continue; }
                    consider(member, member, a.signature, a.doc, a.kind);
                }
                else if (a.name.find('.') == std::string::npos){
                    consider(a.name, a.name, a.signature, a.doc, a.kind);
                }
            }
        };
        addAll(m_Api);
        addAll(GetBuiltins());

        if (object.empty()){
            for (const SnippetDef& s : s_Snippets) { consider(s.label, s.body, "snippet", s.doc, ItemKind::Snippet); }
            for (const std::string& kw : LuaKeywords()) { consider(kw, kw, "", "Lua keyword", ItemKind::Keyword); }

            // Names used elsewhere in this script.
            std::vector<Token> tokens;
            for (int l = 0; l < (int)m_Lines.size(); l++){
                tokens.clear();
                Tokenize(m_Lines[l], m_LineStates[l], &tokens);
                for (const Token& t : tokens){
                    if ((t.kind != Tok::Default && t.kind != Tok::Function) || t.end - t.start < 2) { continue; }
                    if (l == m_Cursor.line && t.start == ws.col) { continue; } // the word being typed
                    std::string w = m_Lines[l].substr(t.start, t.end - t.start);
                    consider(w, w, t.kind == Tok::Function ? "function" : "", "Defined in this script", ItemKind::Variable);
                }
            }
        }

        if (items.empty()) { return; }
        if (items.size() == 1 && items[0].label == prefix && items[0].kind != ItemKind::Snippet) { return; }
        std::sort(items.begin(), items.end(), [](const CompletionEntry& a, const CompletionEntry& b){
            if (a.score != b.score) { return a.score > b.score; }
            return a.label < b.label;
        });
        if (items.size() > 200) { items.resize(200); }
        m_CompItems = std::move(items);
        m_CompOpen = true;
        m_CompStart = ws;
        m_CompSel = 0;
        m_CompScroll = 0;
    }

    void CodeEditor::AcceptCompletion(){
        if (!m_CompOpen || m_CompItems.empty()) { return; }
        const CompletionEntry item = m_CompItems[std::max(0, std::min(m_CompSel, (int)m_CompItems.size() - 1))];
        m_CompOpen = false;

        const std::string& L = m_Lines[m_Cursor.line];
        Coord wordEnd = WordEnd(m_Cursor);
        const char next = wordEnd.col < (int)L.size() ? L[wordEnd.col] : 0;
        std::string before = L.substr(0, m_CompStart.col);
        while (!before.empty() && before.back() == ' ') { before.pop_back(); }
        const bool afterFunctionKeyword = before.size() >= 8 && before.compare(before.size() - 8, 8, "function") == 0;

        std::string part1 = item.insert, part2;
        switch (item.kind){
            case ItemKind::Snippet:{
                std::string indent(LeadingSpaces(L), ' ');
                std::string body;
                for (char c : item.insert){
                    if (c == '\t') { body.append(tabSize, ' '); }
                    else if (c == '\n') { body += '\n'; body += indent; }
                    else { body += c; }
                }
                std::size_t mark = body.find("$0");
                part1 = body.substr(0, mark);
                part2 = mark == std::string::npos ? "" : body.substr(mark + 2);
                break;
            }
            case ItemKind::Function:
                if (next != '(' && item.signature.find('(') != std::string::npos){
                    if (SignatureParams(item.signature).empty()) { part1 += "()"; }
                    else { part1 += "("; part2 = ")"; }
                }
                break;
            case ItemKind::Callback:
                if (next != '('){
                    part1 = (afterFunctionKeyword ? "" : "function ") + item.insert
                          + "(" + SignatureParams(item.signature) + ")";
                }
                break;
            default:
                break;
        }

        BeginEdit(EditKind::Other);
        m_Anchor = m_CompStart;
        m_Cursor = wordEnd;
        ReplaceSelection(part1);
        Coord caret = m_Cursor;
        if (!part2.empty()) { InsertAt(caret, part2); }
        m_Cursor = m_Anchor = caret;
        EndEdit();
    }

    // The function call the cursor is inside on this line, and which argument.
    bool CodeEditor::CallContext(std::string& outName, int& outArg) const{
        const std::string& L = m_Lines[m_Cursor.line];
        std::vector<Token> tokens;
        Tokenize(L, m_LineStates[m_Cursor.line], &tokens);
        int depth = 0;
        outArg = 0;
        for (int i = (int)tokens.size() - 1; i >= 0; i--){
            const Token& t = tokens[i];
            if (t.start >= m_Cursor.col || t.kind != Tok::Punct) { continue; }
            char c = L[t.start];
            if (c == ')' || c == ']' || c == '}') { depth++; }
            else if (c == '(' || c == '[' || c == '{'){
                if (depth > 0) { depth--; continue; }
                if (c != '(') { return false; }
                int p = t.start;
                while (p > 0 && L[p - 1] == ' ') { p--; }
                outName = IdentifierAt({ m_Cursor.line, p });
                return !outName.empty();
            }
            else if (c == ',' && depth == 0) { outArg++; }
        }
        return false;
    }

    // ===================================================================== layout

    float CodeEditor::TextWidth(const char* b, const char* e) const{
        return (m_DrawFont ? m_DrawFont : ImGui::GetFont())->CalcTextSizeA(m_FontSize, FLT_MAX, 0.0f, b, e).x;
    }

    float CodeEditor::XOfCol(int line, int col) const{
        const std::string& s = m_Lines[line];
        col = std::min(col, (int)s.size());
        return TextWidth(s.c_str(), s.c_str() + col);
    }

    int CodeEditor::ColOfX(int line, float x) const{
        if (x <= 0.0f) { return 0; }
        const std::string& s = m_Lines[line];
        float acc = 0.0f;
        int i = 0;
        while (i < (int)s.size()){
            int n = NextCharCol(line, i);
            float w = TextWidth(s.c_str() + i, s.c_str() + n);
            if (x < acc + w * 0.5f) { return i; }
            acc += w;
            i = n;
        }
        return (int)s.size();
    }

    CodeEditor::Coord CodeEditor::ScreenToCoord(const ImVec2& p) const{
        int line = (int)std::floor((p.y - m_Origin.y - kTopPad) / m_LineHeight);
        line = std::max(0, std::min(line, (int)m_Lines.size() - 1));
        return { line, ColOfX(line, p.x - (m_Origin.x + m_TextStart)) };
    }

    void CodeEditor::ScrollToCursor(){
        const float visibleH = m_TextRect.GetHeight();
        const float visibleW = m_TextRect.GetWidth() - m_GutterWidth;
        const float y = kTopPad + m_Cursor.line * m_LineHeight;
        if (m_CenterCursor){
            if (y < m_ScrollY || y + m_LineHeight > m_ScrollY + visibleH)
                ImGui::SetScrollY(std::max(0.0f, y - visibleH * 0.4f));
        }
        else if (y < m_ScrollY + m_LineHeight){
            ImGui::SetScrollY(std::max(0.0f, y - m_LineHeight));
        }
        else if (y + m_LineHeight * 2.0f > m_ScrollY + visibleH){
            ImGui::SetScrollY(y + m_LineHeight * 2.0f - visibleH);
        }
        const float x = XOfCol(m_Cursor.line, m_Cursor.col);
        if (x < m_ScrollX + m_CharWidth * 2.0f) { ImGui::SetScrollX(std::max(0.0f, x - m_CharWidth * 4.0f)); }
        else if (x > m_ScrollX + visibleW - m_CharWidth * 4.0f) { ImGui::SetScrollX(x - visibleW + m_CharWidth * 6.0f); }
    }

    // ===================================================================== input

    void CodeEditor::HandleKeyboard(){
        ImGuiIO& io = ImGui::GetIO();
        const bool ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
        const bool shift = io.KeyShift, alt = io.KeyAlt;
        auto pressed = [](ImGuiKey k){ return ImGui::IsKeyPressed(k, true); };
        const int pageLines = std::max(1, (int)(m_TextRect.GetHeight() / m_LineHeight) - 1);

        if (m_CompOpen){
            int n = (int)m_CompItems.size();
            if (pressed(ImGuiKey_DownArrow))      { m_CompSel = (m_CompSel + 1) % n; return; }
            if (pressed(ImGuiKey_UpArrow))        { m_CompSel = (m_CompSel + n - 1) % n; return; }
            if (pressed(ImGuiKey_PageDown))       { m_CompSel = std::min(n - 1, m_CompSel + kCompRows); return; }
            if (pressed(ImGuiKey_PageUp))         { m_CompSel = std::max(0, m_CompSel - kCompRows); return; }
            const bool enter = pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter);
            if (enter || pressed(ImGuiKey_Tab)){
                const std::string& L = m_Lines[m_Cursor.line];
                std::string typed = L.substr(m_CompStart.col, std::max(0, m_Cursor.col - m_CompStart.col));
                if (!(enter && typed == m_CompItems[m_CompSel].label)){
                    AcceptCompletion();
                    return;
                }
                m_CompOpen = false; // the word is already complete: Enter is a newline
            }
            if (pressed(ImGuiKey_Escape)) { m_CompOpen = false; return; }
        }

        auto vertical = [&](int lines){
            float x = m_PreferredX >= 0.0f ? m_PreferredX : XOfCol(m_Cursor.line, m_Cursor.col);
            int target = m_Cursor.line + lines;
            Coord c;
            if (target < 0) { c = { 0, 0 }; }
            else if (target >= (int)m_Lines.size()) { c = { (int)m_Lines.size() - 1, (int)m_Lines.back().size() }; }
            else { c = { target, ColOfX(target, x) }; }
            SetCursor(c, shift);
            m_PreferredX = x;
            m_CompOpen = false;
        };
        auto horizontal = [&](Coord c){
            SetCursor(c, shift);
            m_PreferredX = -1.0f;
            m_CompOpen = false;
        };

        if (ctrl && !alt){
            if (pressed(ImGuiKey_Z))        { if (shift) { Redo(); } else { Undo(); } }
            else if (pressed(ImGuiKey_Y))   { Redo(); }
            else if (pressed(ImGuiKey_A))   { SelectAll(); }
            else if (pressed(ImGuiKey_C) || pressed(ImGuiKey_Insert)) { Copy(); }
            else if (pressed(ImGuiKey_X))   { Cut(); }
            else if (pressed(ImGuiKey_V))   { Paste(); }
            else if (pressed(ImGuiKey_D))   { DuplicateSelection(); }
            else if (shift && pressed(ImGuiKey_K)) { DeleteLines(); }
            else if (pressed(ImGuiKey_L)){
                int l0 = SelStart().line, l1 = SelEnd().line;
                m_Anchor = { l0, 0 };
                m_Cursor = l1 + 1 < (int)m_Lines.size() ? Coord{ l1 + 1, 0 } : Coord{ l1, (int)m_Lines[l1].size() };
            }
            else if (pressed(ImGuiKey_Slash)) { ToggleComment(); }
            else if (pressed(ImGuiKey_F))   { OpenFind(false); }
            else if (pressed(ImGuiKey_H))   { OpenFind(true); }
            else if (pressed(ImGuiKey_G))   { OpenGoToLine(); }
            else if (pressed(ImGuiKey_S))   { m_SaveRequested = true; }
            else if (pressed(ImGuiKey_Space)) { m_CompExplicit = true; UpdateCompletion(true); }
            else if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd)) { zoom = std::min(3.0f, zoom + 0.1f); }
            else if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract)) { zoom = std::max(0.5f, zoom - 0.1f); }
            else if (pressed(ImGuiKey_0) || pressed(ImGuiKey_Keypad0)) { zoom = 1.0f; }
            else if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)){
                // New line below (Ctrl+Enter) / above (Ctrl+Shift+Enter) without splitting.
                int l = m_Cursor.line;
                std::string pad(LeadingSpaces(m_Lines[l]), ' ');
                BeginEdit(EditKind::Other);
                int at = shift ? l : l + 1;
                m_Lines.insert(m_Lines.begin() + at, pad);
                m_Cursor = m_Anchor = { at, (int)pad.size() };
                EndEdit();
            }
            else if (pressed(ImGuiKey_LeftArrow))  { horizontal(MoveWordLeft(m_Cursor)); }
            else if (pressed(ImGuiKey_RightArrow)) { horizontal(MoveWordRight(m_Cursor)); }
            else if (pressed(ImGuiKey_Home))       { horizontal({ 0, 0 }); }
            else if (pressed(ImGuiKey_End))        { horizontal({ (int)m_Lines.size() - 1, (int)m_Lines.back().size() }); }
            else if (pressed(ImGuiKey_Backspace))  { Backspace(true); }
            else if (pressed(ImGuiKey_Delete))     { DeleteForward(true); }
            else if (pressed(ImGuiKey_UpArrow))    { ImGui::SetScrollY(std::max(0.0f, m_ScrollY - m_LineHeight)); }
            else if (pressed(ImGuiKey_DownArrow))  { ImGui::SetScrollY(m_ScrollY + m_LineHeight); }
            return; // no text input while Ctrl is held
        }

        if (alt && pressed(ImGuiKey_UpArrow))   { MoveLines(-1); return; }
        if (alt && pressed(ImGuiKey_DownArrow)) { MoveLines(1); return; }

        if (pressed(ImGuiKey_LeftArrow)){
            if (HasSelection() && !shift) { horizontal(SelStart()); }
            else if (m_Cursor.col > 0) { horizontal({ m_Cursor.line, PrevCharCol(m_Cursor.line, m_Cursor.col) }); }
            else if (m_Cursor.line > 0) { horizontal({ m_Cursor.line - 1, (int)m_Lines[m_Cursor.line - 1].size() }); }
        }
        else if (pressed(ImGuiKey_RightArrow)){
            if (HasSelection() && !shift) { horizontal(SelEnd()); }
            else if (m_Cursor.col < (int)m_Lines[m_Cursor.line].size()) { horizontal({ m_Cursor.line, NextCharCol(m_Cursor.line, m_Cursor.col) }); }
            else if (m_Cursor.line + 1 < (int)m_Lines.size()) { horizontal({ m_Cursor.line + 1, 0 }); }
        }
        else if (pressed(ImGuiKey_UpArrow))   { vertical(-1); }
        else if (pressed(ImGuiKey_DownArrow)) { vertical(1); }
        else if (pressed(ImGuiKey_PageUp))    { vertical(-pageLines); ImGui::SetScrollY(std::max(0.0f, m_ScrollY - pageLines * m_LineHeight)); }
        else if (pressed(ImGuiKey_PageDown))  { vertical(pageLines); ImGui::SetScrollY(m_ScrollY + pageLines * m_LineHeight); }
        else if (pressed(ImGuiKey_Home)){
            int lead = LeadingSpaces(m_Lines[m_Cursor.line]);
            horizontal({ m_Cursor.line, m_Cursor.col == lead ? 0 : lead }); // smart home
        }
        else if (pressed(ImGuiKey_End))       { horizontal({ m_Cursor.line, (int)m_Lines[m_Cursor.line].size() }); }
        else if (pressed(ImGuiKey_Backspace)) { Backspace(false); }
        else if (pressed(ImGuiKey_Delete))    { DeleteForward(false); }
        else if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) { NewLine(); m_CompOpen = false; }
        else if (pressed(ImGuiKey_Tab)){
            if (shift || HasSelection()) { IndentLines(shift); }
            else{
                BeginEdit(EditKind::Typing);
                ReplaceSelection(std::string(tabSize - m_Cursor.col % tabSize, ' '));
                EndEdit();
            }
        }
        else if (pressed(ImGuiKey_Escape)){
            if (m_FindOpen || m_GoToOpen) { m_FindOpen = m_GoToOpen = false; }
            else if (HasSelection()) { m_Anchor = m_Cursor; }
            else { ImGui::ClearActiveID(); m_Focused = false; return; }
        }
        else if (pressed(ImGuiKey_F3)){
            if (!m_FindBuf[0]) { OpenFind(false); }
            else { FindNext(!shift); }
        }

        if (!io.InputQueueCharacters.empty() && !(io.KeyCtrl && !io.KeyAlt)){
            for (ImWchar c : io.InputQueueCharacters){
                if (c == '\t' || c == '\n' || c == '\r') { continue; } // handled as keys
                TypeChar(c);
            }
        }
    }

    void CodeEditor::HandleMouse(bool hovered, const ImRect& textRect){
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse = io.MousePos;
        const int lineCount = (int)m_Lines.size();
        auto lineSpan = [&](int l, Coord& s, Coord& e){
            s = { l, 0 };
            e = l + 1 < lineCount ? Coord{ l + 1, 0 } : Coord{ l, (int)m_Lines[l].size() };
        };

        // Completion popup: wheel scrolls it, click accepts an item.
        if (hovered && m_CompOpen && m_CompRect.Contains(mouse)){
            if (io.MouseWheel != 0.0f){
                int maxScroll = std::max(0, (int)m_CompItems.size() - kCompRows);
                m_CompScroll = std::max(0, std::min(maxScroll, m_CompScroll - (int)io.MouseWheel));
                ImGui::SetItemUsingMouseWheel();
            }
            if (ImGui::IsMouseClicked(0)){
                int row = (int)((mouse.y - m_CompRect.Min.y - 2.0f) / m_CompRowHeight) + m_CompScroll;
                if (row >= m_CompScroll && row < std::min((int)m_CompItems.size(), m_CompScroll + kCompRows)){
                    m_CompSel = row;
                    AcceptCompletion();
                }
            }
            return;
        }

        if (hovered && io.KeyCtrl && io.MouseWheel != 0.0f){
            zoom = std::max(0.5f, std::min(3.0f, zoom + io.MouseWheel * 0.1f));
            ImGui::SetItemUsingMouseWheel();
        }

        if (hovered && ImGui::IsMouseClicked(0)){
            const int clicks = ImGui::GetMouseClickedCount(0);
            const Coord c = ScreenToCoord(mouse);
            const bool inGutter = mouse.x < textRect.Min.x + m_GutterWidth;
            m_CompOpen = false;
            m_PreferredX = -1.0f;
            m_CaretTime = ImGui::GetTime();
            if (inGutter || clicks >= 3){
                m_SelectMode = 2;
                lineSpan(c.line, m_SelectOriginStart, m_SelectOriginEnd);
                if (io.KeyShift && inGutter){
                    Coord s, e;
                    lineSpan(c.line, s, e);
                    if (c.line < m_Anchor.line) { m_Cursor = s; } else { m_Cursor = e; }
                }
                else{
                    m_Anchor = m_SelectOriginStart;
                    m_Cursor = m_SelectOriginEnd;
                }
            }
            else if (clicks == 2){
                m_SelectMode = 1;
                Coord s = WordStart(c), e = WordEnd(c);
                if (s == e) { e.col = NextCharCol(c.line, c.col); }
                m_SelectOriginStart = s;
                m_SelectOriginEnd = e;
                m_Anchor = s;
                m_Cursor = e;
            }
            else{
                m_SelectMode = 0;
                m_Cursor = c;
                if (!io.KeyShift) { m_Anchor = c; }
            }
            m_Selecting = true;
        }
        else if (m_Selecting){
            if (!ImGui::IsMouseDown(0)) { m_Selecting = false; }
            else{
                // Auto-scroll while dragging past the edges.
                if (mouse.y < textRect.Min.y) { ImGui::SetScrollY(std::max(0.0f, m_ScrollY - m_LineHeight)); }
                else if (mouse.y > textRect.Max.y) { ImGui::SetScrollY(m_ScrollY + m_LineHeight); }
                if (mouse.x < textRect.Min.x + m_GutterWidth && m_ScrollX > 0.0f) { ImGui::SetScrollX(std::max(0.0f, m_ScrollX - m_CharWidth * 2.0f)); }
                else if (mouse.x > textRect.Max.x) { ImGui::SetScrollX(m_ScrollX + m_CharWidth * 2.0f); }

                const Coord c = ScreenToCoord(mouse);
                if (m_SelectMode == 0) { m_Cursor = c; }
                else if (m_SelectMode == 1){
                    if (c < m_SelectOriginStart) { m_Anchor = m_SelectOriginEnd; m_Cursor = WordStart(c); }
                    else { m_Anchor = m_SelectOriginStart; m_Cursor = std::max(WordEnd(c), m_SelectOriginEnd); }
                }
                else{
                    Coord s, e;
                    lineSpan(c.line, s, e);
                    if (c.line < m_SelectOriginStart.line) { m_Anchor = m_SelectOriginEnd; m_Cursor = s; }
                    else { m_Anchor = m_SelectOriginStart; m_Cursor = e; }
                }
            }
        }

        if (hovered && ImGui::IsMouseClicked(1)){
            Coord c = ScreenToCoord(mouse);
            if (!HasSelection() || c < SelStart() || c > SelEnd()) { m_Cursor = m_Anchor = c; }
            m_OpenContextMenu = true;
            m_CompOpen = false;
        }
    }

    // ===================================================================== drawing

    void CodeEditor::DrawText(ImDrawList* dl, int first, int last){
        const Palette& pal = Colors();
        ImFont* fnt = ImGui::GetFont();
        const float xText = m_Origin.x + m_TextStart;
        const float yOff = (m_LineHeight - m_FontSize) * 0.5f;
        auto lineTop = [&](int l){ return m_Origin.y + kTopPad + l * m_LineHeight; };
        auto rect = [&](int l, float x0, float x1, ImU32 col, bool filled = true){
            ImVec2 a(xText + x0, lineTop(l)), b(xText + x1, lineTop(l) + m_LineHeight);
            if (filled) { dl->AddRectFilled(a, b, col, 2.0f); }
            else { dl->AddRect(a, b, col, 2.0f); }
        };

        // Current line / diagnostics backgrounds.
        if (highlightCurrentLine && !HasSelection() && m_Cursor.line >= first && m_Cursor.line <= last){
            float y = lineTop(m_Cursor.line);
            dl->AddRectFilled(ImVec2(m_TextRect.Min.x, y), ImVec2(m_TextRect.Max.x, y + m_LineHeight), pal.currentLine);
        }
        for (const Diagnostic& d : m_Diagnostics){
            int l = d.line - 1;
            if (l < first || l > last) { continue; }
            float y = lineTop(l);
            dl->AddRectFilled(ImVec2(m_TextRect.Min.x, y), ImVec2(m_TextRect.Max.x, y + m_LineHeight), d.error ? pal.errorBg : pal.warningBg);
        }

        // Find matches.
        if (m_FindOpen && m_FindBuf[0]){
            UpdateMatches();
            for (const Match& m : m_Matches){
                if (m.start.line < first || m.start.line > last) { continue; }
                bool current = m.start == SelStart() && m.end == SelEnd();
                rect(m.start.line, XOfCol(m.start.line, m.start.col), XOfCol(m.end.line, m.end.col),
                     current ? pal.findCurrent : pal.findMatch, !current);
                if (current) { rect(m.start.line, XOfCol(m.start.line, m.start.col), XOfCol(m.end.line, m.end.col), pal.findMatch); }
            }
        }

        // Other occurrences of the word at the cursor (or the selected word).
        std::string occurrence;
        if (!HasSelection()){
            Coord s = WordStart(m_Cursor), e = WordEnd(m_Cursor);
            if (e.col - s.col >= 2) { occurrence = m_Lines[s.line].substr(s.col, e.col - s.col); }
        }
        else if (SelStart().line == SelEnd().line){
            std::string sel = GetRange(SelStart(), SelEnd());
            if (sel.size() >= 2 && std::all_of(sel.begin(), sel.end(), IsIdentChar)) { occurrence = sel; }
        }
        if (!occurrence.empty() && !LuaKeywords().count(occurrence) && !std::isdigit((unsigned char)occurrence[0])){
            for (int l = first; l <= last; l++){
                const std::string& L = m_Lines[l];
                for (std::size_t p = L.find(occurrence); p != std::string::npos; p = L.find(occurrence, p + 1)){
                    std::size_t e = p + occurrence.size();
                    if ((p > 0 && IsIdentChar(L[p - 1])) || (e < L.size() && IsIdentChar(L[e]))) { continue; }
                    rect(l, XOfCol(l, (int)p), XOfCol(l, (int)e), pal.occurrence);
                }
            }
        }

        // Selection.
        if (HasSelection()){
            Coord s = SelStart(), e = SelEnd();
            for (int l = std::max(first, s.line); l <= std::min(last, e.line); l++){
                float x0 = l == s.line ? XOfCol(l, s.col) : 0.0f;
                float x1 = l == e.line ? XOfCol(l, e.col) : XOfCol(l, (int)m_Lines[l].size()) + m_CharWidth * 0.5f;
                if (x1 > x0) { rect(l, x0, x1, pal.selection); }
            }
        }

        // Matching bracket pair.
        Coord bo, bc;
        if (m_Focused && !HasSelection() && FindMatchingBracket(m_Cursor, bo, bc)){
            for (Coord c : { bo, bc })
                if (c.line >= first && c.line <= last)
                    rect(c.line, XOfCol(c.line, c.col), XOfCol(c.line, c.col + 1), pal.bracket, false);
        }

        // Indent guides (blank lines continue the guides around them).
        if (showIndentGuides){
            for (int l = first; l <= last; l++){
                int indent = 0;
                if (IsBlank(m_Lines[l])){
                    int p = l - 1, n = l + 1;
                    while (p >= 0 && IsBlank(m_Lines[p])) { p--; }
                    while (n < (int)m_Lines.size() && IsBlank(m_Lines[n])) { n++; }
                    int a = p >= 0 ? LeadingSpaces(m_Lines[p]) : 0;
                    int b = n < (int)m_Lines.size() ? LeadingSpaces(m_Lines[n]) : 0;
                    indent = std::min(a, b);
                    if (a != b) { indent = std::max(indent, std::min(std::max(a, b), std::min(a, b) + tabSize)); }
                }
                else { indent = LeadingSpaces(m_Lines[l]); }
                for (int k = 0; k < indent; k += tabSize){
                    float x = std::floor(xText + k * m_CharWidth) + 0.5f;
                    dl->AddLine(ImVec2(x, lineTop(l)), ImVec2(x, lineTop(l) + m_LineHeight), pal.indentGuide);
                }
            }
        }

        // Text.
        std::vector<Token> tokens;
        for (int l = first; l <= last; l++){
            const std::string& L = m_Lines[l];
            tokens.clear();
            Tokenize(L, m_LineStates[l], &tokens);
            const char* s = L.c_str();
            const float y = lineTop(l) + yOff;
            float x = xText;
            int col = 0;
            for (const Token& t : tokens){
                x += TextWidth(s + col, s + t.start);
                dl->AddText(fnt, m_FontSize, ImVec2(x, y), TokenColor((int)t.kind), s + t.start, s + t.end);
                x += TextWidth(s + t.start, s + t.end);
                col = t.end;
            }
            if (showWhitespace){
                for (int i = 0; i < (int)L.size(); i++){
                    if (L[i] != ' ') { continue; }
                    float cx = xText + XOfCol(l, i) + m_CharWidth * 0.5f;
                    dl->AddCircleFilled(ImVec2(cx, lineTop(l) + m_LineHeight * 0.5f), 1.2f, pal.whitespace);
                }
            }
        }

        // Diagnostics: squiggle under the line and the message after it.
        for (const Diagnostic& d : m_Diagnostics){
            int l = d.line - 1;
            if (l < first || l > last) { continue; }
            const std::string& L = m_Lines[l];
            float x0 = xText + XOfCol(l, LeadingSpaces(L));
            float x1 = xText + XOfCol(l, (int)L.size());
            if (x1 - x0 < m_CharWidth * 2.0f) { x1 = x0 + m_CharWidth * 2.0f; }
            ImU32 col = d.error ? pal.error : pal.warning;
            float y = lineTop(l) + m_LineHeight - 2.5f;
            for (float x = x0; x < x1; x += 3.0f)
                dl->AddLine(ImVec2(x, y + ((int)((x - x0) / 3.0f) % 2 ? 1.5f : -0.5f)),
                            ImVec2(std::min(x + 3.0f, x1), y + ((int)((x - x0) / 3.0f) % 2 ? -0.5f : 1.5f)), col, 1.0f);
            std::string msg = d.message;
            std::size_t nl = msg.find('\n');
            if (nl != std::string::npos) { msg = msg.substr(0, nl); }
            dl->AddText(fnt, m_FontSize, ImVec2(x1 + m_CharWidth * 3.0f, lineTop(l) + yOff),
                        (col & 0x00FFFFFF) | 0xB0000000, msg.c_str());
        }

        // Caret.
        if (m_Focused){
            const double t = ImGui::GetTime() - m_CaretTime;
            if (!ImGui::GetIO().ConfigInputTextCursorBlink || std::fmod(t, 1.1) < 0.65 || m_Selecting){
                float x = std::floor(xText + XOfCol(m_Cursor.line, m_Cursor.col));
                float y = lineTop(m_Cursor.line);
                dl->AddRectFilled(ImVec2(x, y + 1.0f), ImVec2(x + 2.0f, y + m_LineHeight - 1.0f), pal.caret);
            }
        }
    }

    void CodeEditor::DrawGutter(ImDrawList* dl, int first, int last){
        const Palette& pal = Colors();
        ImFont* fnt = ImGui::GetFont();
        const float x0 = m_TextRect.Min.x, x1 = x0 + m_GutterWidth;
        const float yOff = (m_LineHeight - m_FontSize) * 0.5f;
        dl->AddRectFilled(ImVec2(x0, m_TextRect.Min.y), ImVec2(x1, m_TextRect.Max.y), pal.gutterBg);
        dl->AddLine(ImVec2(x1 - 1.0f, m_TextRect.Min.y), ImVec2(x1 - 1.0f, m_TextRect.Max.y), pal.gutterSep);

        std::unordered_map<int, bool> marks; // line -> has error (else warning)
        for (const Diagnostic& d : m_Diagnostics) { marks[d.line - 1] = marks[d.line - 1] || d.error; }

        const Coord s = SelStart(), e = SelEnd();
        for (int l = first; l <= last; l++){
            const float y = m_Origin.y + kTopPad + l * m_LineHeight;
            if (showLineNumbers){
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", l + 1);
                bool hi = l == m_Cursor.line || (HasSelection() && l >= s.line && l <= e.line);
                float w = TextWidth(buf, buf + std::strlen(buf));
                dl->AddText(fnt, m_FontSize, ImVec2(x1 - w - m_CharWidth * 1.2f, y + yOff), hi ? pal.lineNumberHi : pal.lineNumber, buf);
            }
            auto it = marks.find(l);
            if (it != marks.end()){
                dl->AddCircleFilled(ImVec2(x0 + m_CharWidth * 0.75f, y + m_LineHeight * 0.5f),
                                    std::max(2.5f, m_CharWidth * 0.3f), it->second ? pal.error : pal.warning);
            }
        }
    }

    void CodeEditor::DrawCompletion(ImDrawList* dl){
        if (!m_CompOpen || m_CompItems.empty()) { return; }
        const Palette& pal = Colors();
        ImFont* fnt = ImGui::GetFont();
        const int count = (int)m_CompItems.size();
        const int rows = std::min(count, kCompRows);
        m_CompSel = std::max(0, std::min(m_CompSel, count - 1));
        if (m_CompSel < m_CompScroll) { m_CompScroll = m_CompSel; }
        if (m_CompSel >= m_CompScroll + rows) { m_CompScroll = m_CompSel - rows + 1; }

        const float rowH = m_LineHeight;
        const float width = std::min(m_TextRect.GetWidth() - 8.0f, m_CharWidth * 64.0f);
        const CompletionEntry& sel = m_CompItems[m_CompSel];
        std::string docText = sel.doc;
        const float docWrap = width - 12.0f;
        float docH = 0.0f;
        bool showSig = !sel.signature.empty() && sel.kind != ItemKind::Snippet && sel.signature != sel.label;
        if (showSig) { docH += m_LineHeight; }
        if (!docText.empty()) { docH += fnt->CalcTextSizeA(m_FontSize, FLT_MAX, docWrap, docText.c_str()).y + 4.0f; }
        if (docH > 0.0f) { docH += 6.0f; }
        const float height = rows * rowH + 4.0f + docH;

        const float lineTop = m_Origin.y + kTopPad + m_CompStart.line * m_LineHeight;
        ImVec2 pos(m_Origin.x + m_TextStart + XOfCol(m_CompStart.line, m_CompStart.col) - m_CharWidth * 2.0f,
                   lineTop + m_LineHeight + 2.0f);
        if (pos.y + height > m_TextRect.Max.y && lineTop - height - 2.0f >= m_TextRect.Min.y) { pos.y = lineTop - height - 2.0f; }
        pos.x = std::max(m_TextRect.Min.x + m_GutterWidth, std::min(pos.x, m_TextRect.Max.x - width - 4.0f));
        pos.y = std::max(m_TextRect.Min.y, pos.y);
        m_CompRect = ImRect(pos, ImVec2(pos.x + width, pos.y + height));
        m_CompRowHeight = rowH;

        dl->AddRectFilled(m_CompRect.Min, m_CompRect.Max, pal.popupBg, 4.0f);
        dl->AddRect(m_CompRect.Min, m_CompRect.Max, pal.popupBorder, 4.0f);

        const std::string& L = m_Lines[m_Cursor.line];
        const std::string prefix = L.substr(m_CompStart.col, std::max(0, m_Cursor.col - m_CompStart.col));
        const float yOff = (rowH - m_FontSize) * 0.5f;
        for (int i = m_CompScroll; i < m_CompScroll + rows; i++){
            const CompletionEntry& it = m_CompItems[i];
            ImVec2 a(pos.x + 2.0f, pos.y + 2.0f + (i - m_CompScroll) * rowH), b(pos.x + width - 2.0f, a.y + rowH);
            if (i == m_CompSel) { dl->AddRectFilled(a, b, pal.popupSel, 3.0f); }
            dl->PushClipRect(a, b, true);
            // Kind badge.
            ImVec2 ba(a.x + 4.0f, a.y + 3.0f), bb(a.x + 4.0f + m_CharWidth * 1.6f, b.y - 3.0f);
            ImU32 kc = KindColor(it.kind);
            dl->AddRectFilled(ba, bb, (kc & 0x00FFFFFF) | 0x40000000, 3.0f);
            const char* letter = KindLetter(it.kind);
            dl->AddText(fnt, m_FontSize, ImVec2((ba.x + bb.x - m_CharWidth) * 0.5f, a.y + yOff), kc, letter);
            // Label, matched prefix in the accent colour.
            float x = bb.x + m_CharWidth * 0.8f;
            std::size_t hl = (it.label.size() >= prefix.size() && ToLower(it.label.substr(0, prefix.size())) == ToLower(prefix))
                             ? prefix.size() : 0;
            const char* lb = it.label.c_str();
            dl->AddText(fnt, m_FontSize, ImVec2(x, a.y + yOff), pal.accent, lb, lb + hl);
            x += TextWidth(lb, lb + hl);
            dl->AddText(fnt, m_FontSize, ImVec2(x, a.y + yOff), pal.text, lb + hl);
            x += TextWidth(lb + hl, lb + it.label.size()) + m_CharWidth * 2.0f;
            // Right-aligned detail.
            std::string detail = it.kind == ItemKind::Snippet ? "snippet" : it.signature;
            if (!detail.empty() && detail != it.label){
                float w = TextWidth(detail.c_str(), detail.c_str() + detail.size());
                float dx = std::max(x, b.x - w - 6.0f);
                dl->AddText(fnt, m_FontSize, ImVec2(dx, a.y + yOff), pal.dim, detail.c_str());
            }
            dl->PopClipRect();
        }
        if (count > rows){
            float trackH = rows * rowH;
            float thumbH = std::max(12.0f, trackH * rows / count);
            float thumbY = pos.y + 2.0f + (trackH - thumbH) * m_CompScroll / (float)(count - rows);
            dl->AddRectFilled(ImVec2(pos.x + width - 4.0f, thumbY), ImVec2(pos.x + width - 1.5f, thumbY + thumbH), pal.popupBorder, 1.5f);
        }
        if (docH > 0.0f){
            float y = pos.y + 4.0f + rows * rowH;
            dl->AddLine(ImVec2(pos.x + 4.0f, y), ImVec2(pos.x + width - 4.0f, y), pal.popupBorder);
            y += 4.0f;
            if (showSig){
                dl->AddText(fnt, m_FontSize, ImVec2(pos.x + 6.0f, y), pal.accent, sel.signature.c_str());
                y += m_LineHeight;
            }
            if (!docText.empty())
                dl->AddText(fnt, m_FontSize, ImVec2(pos.x + 6.0f, y), pal.dim, docText.c_str(), nullptr, docWrap);
        }
    }

    void CodeEditor::DrawSignatureHelp(ImDrawList* dl){
        if (!m_Focused || HasSelection()) { return; }
        std::string name;
        int arg = 0;
        if (!CallContext(name, arg)) { return; }
        const ApiItem* api = LookupApi(name);
        if (!api || api->kind != ItemKind::Function) { return; }
        const std::string& sig = api->signature;
        std::size_t open = sig.find('('), close = sig.find(')', open == std::string::npos ? 0 : open);
        if (open == std::string::npos || close == std::string::npos) { return; }

        const Palette& pal = Colors();
        ImFont* fnt = ImGui::GetFont();
        // Split the parameter list on commas; highlight the argument being typed.
        std::vector<std::pair<std::string, bool>> parts;
        parts.push_back({ sig.substr(0, open + 1), false });
        std::string params = sig.substr(open + 1, close - open - 1);
        int index = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= params.size(); i++){
            if (i == params.size() || params[i] == ','){
                std::string piece = params.substr(start, i - start);
                parts.push_back({ piece, index == arg });
                if (i < params.size()) { parts.push_back({ ",", false }); }
                index++;
                start = i + 1;
            }
        }
        parts.push_back({ sig.substr(close), false });

        float sigW = 0.0f;
        for (auto& p : parts) { sigW += TextWidth(p.first.c_str(), p.first.c_str() + p.first.size()); }
        const float wrap = std::min(m_CharWidth * 60.0f, m_TextRect.GetWidth() - 16.0f);
        const float docH = api->doc.empty() ? 0.0f : fnt->CalcTextSizeA(m_FontSize, FLT_MAX, wrap, api->doc.c_str()).y;
        const float width = std::min(std::max(sigW, api->doc.empty() ? 0.0f : std::min(wrap, TextWidth(api->doc.c_str(), api->doc.c_str() + api->doc.size()))) + 12.0f,
                                     m_TextRect.GetWidth() - 8.0f);
        const float height = m_LineHeight + (docH > 0.0f ? docH + 4.0f : 0.0f) + 6.0f;

        const float lineTop = m_Origin.y + kTopPad + m_Cursor.line * m_LineHeight;
        ImVec2 pos(m_Origin.x + m_TextStart + XOfCol(m_Cursor.line, m_Cursor.col) - m_CharWidth * 4.0f, lineTop - height - 2.0f);
        if (pos.y < m_TextRect.Min.y) { pos.y = lineTop + m_LineHeight + 2.0f; }
        pos.x = std::max(m_TextRect.Min.x + m_GutterWidth, std::min(pos.x, m_TextRect.Max.x - width - 4.0f));

        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), pal.popupBg, 4.0f);
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), pal.popupBorder, 4.0f);
        float x = pos.x + 6.0f, y = pos.y + 3.0f + (m_LineHeight - m_FontSize) * 0.5f;
        for (auto& p : parts){
            const char* b = p.first.c_str();
            const char* e = b + p.first.size();
            float w = TextWidth(b, e);
            dl->AddText(fnt, m_FontSize, ImVec2(x, y), p.second ? pal.accent : pal.text, b, e);
            if (p.second) { dl->AddLine(ImVec2(x, y + m_FontSize + 1.0f), ImVec2(x + w, y + m_FontSize + 1.0f), pal.accent); }
            x += w;
        }
        if (docH > 0.0f)
            dl->AddText(fnt, m_FontSize, ImVec2(pos.x + 6.0f, pos.y + 3.0f + m_LineHeight + 2.0f), pal.dim, api->doc.c_str(), nullptr, wrap);
    }

    void CodeEditor::DrawHoverHelp(){
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse = io.MousePos;
        const double now = ImGui::GetTime();
        if (m_Selecting || ImGui::IsMouseDown(0) || (m_CompOpen && m_CompRect.Contains(mouse))){
            m_HoverStart = now;
            return;
        }
        if (std::fabs(mouse.x - m_HoverPos.x) > 2.0f || std::fabs(mouse.y - m_HoverPos.y) > 2.0f){
            m_HoverPos = mouse;
            m_HoverStart = now;
            return;
        }
        if (now - m_HoverStart < 0.45) { return; }

        const Coord c = ScreenToCoord(mouse);
        const bool inGutter = mouse.x < m_TextRect.Min.x + m_GutterWidth;
        const float lineEndX = m_Origin.x + m_TextStart + XOfCol(c.line, (int)m_Lines[c.line].size());

        std::vector<const Diagnostic*> diags;
        for (const Diagnostic& d : m_Diagnostics)
            if (d.line - 1 == c.line) { diags.push_back(&d); }

        const ApiItem* api = nullptr;
        if (!inGutter && mouse.x <= lineEndX){
            Tok k = KindAt({ c.line, std::min(c.col + 1, (int)m_Lines[c.line].size()) });
            if (k != Tok::String && k != Tok::Comment){
                std::string id = IdentifierAt(c);
                api = LookupApi(id);
                if (!api && id.find_last_of(".:") != std::string::npos) { api = LookupApi(id.substr(id.find_last_of(".:") + 1)); }
            }
        }
        if (!api && diags.empty()) { return; }
        if (!api && !inGutter && mouse.x > lineEndX + m_CharWidth * 3.0f + 400.0f) { return; }

        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        if (api){
            if (font) { ImGui::PushFont(font); }
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors().accent), "%s", api->signature.c_str());
            if (font) { ImGui::PopFont(); }
            if (!api->doc.empty()) { ImGui::TextUnformatted(api->doc.c_str()); }
        }
        for (const Diagnostic* d : diags){
            if (api) { ImGui::Separator(); }
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(d->error ? Colors().error : Colors().warning),
                               "%s (line %d)", d->message.c_str(), d->line);
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    void CodeEditor::DrawContextMenu(){
        if (m_OpenContextMenu){
            ImGui::OpenPopup("##codecontext");
            m_OpenContextMenu = false;
        }
        if (!ImGui::BeginPopup("##codecontext")) { return; }
        bool act = false;
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, CanUndo())) { Undo(); act = true; }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, CanRedo())) { Redo(); act = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Cut", "Ctrl+X")) { Cut(); act = true; }
        if (ImGui::MenuItem("Copy", "Ctrl+C")) { Copy(); act = true; }
        if (ImGui::MenuItem("Paste", "Ctrl+V")) { Paste(); act = true; }
        if (ImGui::MenuItem("Select All", "Ctrl+A")) { SelectAll(); act = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Toggle Comment", "Ctrl+/")) { ToggleComment(); act = true; }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) { DuplicateSelection(); act = true; }
        if (ImGui::MenuItem("Delete Line", "Ctrl+Shift+K")) { DeleteLines(); act = true; }
        if (ImGui::MenuItem("Indent", "Tab")) { IndentLines(false); act = true; }
        if (ImGui::MenuItem("Unindent", "Shift+Tab")) { IndentLines(true); act = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Find", "Ctrl+F")) { OpenFind(false); }
        if (ImGui::MenuItem("Replace", "Ctrl+H")) { OpenFind(true); }
        if (ImGui::MenuItem("Go to Line", "Ctrl+G")) { OpenGoToLine(); }
        ImGui::EndPopup();
        if (act) { m_WantFocus = true; }
    }

    // ===================================================================== render

    bool CodeEditor::Render(const char* id, const ImVec2& sizeArg){
        ImGuiContext& g = *GImGui;
        ImGuiIO& io = g.IO;
        ImGui::PushID(id);

        if (m_FindOpen) { DrawFindBar(); }
        else if (m_GoToOpen) { DrawGoToBar(); }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 size(sizeArg.x > 0.0f ? sizeArg.x : avail.x, sizeArg.y > 0.0f ? sizeArg.y : avail.y);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors().bg);
        ImGui::BeginChild("##code", size, false,
            ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGuiWindow* window = ImGui::GetCurrentWindow();

        if (font) { ImGui::PushFont(font); }
        ImGui::SetWindowFontScale(zoom);
        m_FontSize = ImGui::GetFontSize();
        m_DrawFont = ImGui::GetFont();
        m_LineHeight = std::floor(m_FontSize * 1.45f);
        const char* glyph = "M";
        m_CharWidth = TextWidth(glyph, glyph + 1);

        UpdateLineStates();
        int digits = 1;
        for (int n = (int)m_Lines.size(); n >= 10; n /= 10) { digits++; }
        m_GutterWidth = showLineNumbers ? (std::max(digits, 3) + 2.5f) * m_CharWidth : m_CharWidth * 1.8f;
        m_TextStart = m_GutterWidth + m_CharWidth * 0.8f;
        m_ScrollX = ImGui::GetScrollX();
        m_ScrollY = ImGui::GetScrollY();
        m_Origin = ImGui::GetCursorScreenPos();
        m_TextRect = ImRect(window->InnerRect.Min, window->InnerRect.Max);

        // The text area is one ImGui item. While focused it is the active item: it
        // owns the keyboard (no nav, io.WantTextInput set) like a text field does.
        const ImGuiID itemId = window->GetID("##text");
        ImGui::ItemAdd(m_TextRect, itemId);
        const bool hovered = ImGui::ItemHoverable(m_TextRect, itemId);
        if (hovered) { ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput); }
        const bool clicked = hovered && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1));
        if ((clicked || m_WantFocus) && g.ActiveId != itemId){
            ImGui::SetActiveID(itemId, window);
            ImGui::SetFocusID(itemId, window);
            ImGui::FocusWindow(window);
            m_CaretTime = ImGui::GetTime();
        }
        m_WantFocus = false;
        m_Focused = g.ActiveId == itemId;
        if (m_Focused){
            ImGui::KeepAliveID(itemId);
            g.ActiveIdAllowOverlap = !io.MouseDown[0];
            g.WantTextInputNextFrame = 1;
            g.ActiveIdUsingNavDirMask = ~(ImU32)0;
            g.ActiveIdUsingKeyInputMask.SetAllBits();
            if (io.MouseClicked[0] && !hovered && !m_Selecting){
                ImGui::ClearActiveID();
                m_Focused = false;
            }
        }

        if (m_Focused) { HandleKeyboard(); }
        HandleMouse(hovered, m_TextRect);
        if (!m_Focused) { m_CompOpen = false; }
        UpdateLineStates();
        if (m_ScrollToCursor){
            ScrollToCursor();
            m_ScrollToCursor = false;
            m_CenterCursor = false;
        }

        // Content size (lets the last line scroll up to the middle of the view).
        ImGui::Dummy(ImVec2(m_TextStart + (m_LongestLine + 8) * m_CharWidth,
                            kTopPad + m_Lines.size() * m_LineHeight + m_TextRect.GetHeight() * 0.5f));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const int first = std::max(0, (int)((m_ScrollY - kTopPad) / m_LineHeight));
        const int last = std::min((int)m_Lines.size() - 1, (int)((m_ScrollY + m_TextRect.GetHeight()) / m_LineHeight) + 1);
        dl->PushClipRect(ImVec2(m_TextRect.Min.x + m_GutterWidth, m_TextRect.Min.y), m_TextRect.Max, true);
        DrawText(dl, first, last);
        dl->PopClipRect();
        DrawGutter(dl, first, last);
        dl->PushClipRect(m_TextRect.Min, m_TextRect.Max, true);
        if (m_CompOpen) { DrawCompletion(dl); }
        else { DrawSignatureHelp(dl); }
        dl->PopClipRect();

        if (font) { ImGui::PopFont(); }
        ImGui::SetWindowFontScale(1.0f);
        if (hovered) { DrawHoverHelp(); }
        else { m_HoverStart = ImGui::GetTime(); }
        DrawContextMenu();

        ImGui::EndChild();
        ImGui::PopID();

        bool changed = m_Changed;
        m_Changed = false;
        return changed;
    }
}
