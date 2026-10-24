#include <Phoenix/imGui/ImGuiLayer.h>
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_impl_glfw.h>
#include <Phoenix/imGui/imgui_impl_opengl3.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <Phoenix/core/application.h>

namespace Phoenix{
    ImGuiLayer::ImGuiLayer(): Layer("ImGuiLayer"){}

	void ImGuiLayer::OnAttach(){
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
        io.ConfigWindowsMoveFromTitleBarOnly = true;
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoMerge;
		io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Bold.ttf", 15.0f);
		io.FontDefault = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Regular.ttf", 15.0f);
		io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Bold.ttf", 17.0f);
		io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Regular.ttf", 13.0f);

		// Code font for the script editor: JetBrains Mono (SIL OFL, see
		// JetBrainsMono-OFL.txt), falling back to the mono font compiled into ImGui.
		// Added after the four fonts above so the FontType indices stay valid.
		s_MonospaceFont = io.Fonts->AddFontFromFileTTF("assets/fonts/JetBrainsMono-Regular.ttf", 15.0f);
		if (!s_MonospaceFont)
			s_MonospaceFont = io.Fonts->AddFontDefault();

		s_HudFont = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Bold.ttf", 20.0f);
		s_BannerFont = io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Bold.ttf", 44.0f);

		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowPadding     = ImVec2(10.0f, 8.0f);
		style.FramePadding      = ImVec2(8.0f, 4.0f);
		style.CellPadding       = ImVec2(6.0f, 3.0f);
		style.ItemSpacing       = ImVec2(8.0f, 6.0f);
		style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
		style.IndentSpacing     = 14.0f;
		style.ScrollbarSize     = 11.0f;
		style.GrabMinSize       = 10.0f;

		style.WindowBorderSize  = 1.0f;
		style.ChildBorderSize   = 1.0f;
		style.PopupBorderSize   = 1.0f;
		style.FrameBorderSize   = 0.0f;
		style.TabBorderSize     = 0.0f;

		style.WindowRounding    = 0.0f;
		style.ChildRounding     = 4.0f;
		style.FrameRounding     = 4.0f;
		style.PopupRounding     = 6.0f;
		style.ScrollbarRounding = 8.0f;
		style.GrabRounding      = 3.0f;
		style.TabRounding       = 4.0f;

		style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
		// No collapse triangle in front of every docked tab bar.
		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		SetDarkThemeColors();

		Application& app = Application::Get();
		GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

		ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 130");
        PHX_CORE_INFO("{0} attached.", this->layer_name);
	}

	void ImGuiLayer::OnDetach(){
        ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
        PHX_CORE_INFO("{0} detached.", this->layer_name);
	}

	void ImGuiLayer::OnEvent(Event& e){
		if (_blockEvents){
			ImGuiIO& io = ImGui::GetIO();
			e.Handled |= e.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
			e.Handled |= e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
		}
	}
	
	void ImGuiLayer::Begin(){
		ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void ImGuiLayer::End(){
		ImGuiIO& io = ImGui::GetIO();
		Application& app = Application::Get();
		io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());
		// Rendering
		ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	ImFont* ImGuiLayer::GetFont(FontType type){
		ImFontAtlas* atlas = ImGui::GetIO().Fonts;
		int index = (int)type;
		return index < atlas->Fonts.Size ? atlas->Fonts[index] : ImGui::GetFont();
	}

	void ImGuiLayer::SetDarkThemeColors(){
		// Neutral charcoal surfaces in three depths (chrome < panels < fields) with a
		// single warm "phoenix" accent for focus, selection and active state.
		auto rgb = [](int r, int g, int b, float a = 1.0f){ return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
		const ImVec4 chrome    = rgb(22, 23, 26);   // title bar, tab strips, status bar
		const ImVec4 panel     = rgb(30, 31, 35);   // window contents
		const ImVec4 raised    = rgb(39, 41, 46);   // popups, headers
		const ImVec4 field     = rgb(43, 45, 51);   // inputs, buttons
		const ImVec4 hover     = rgb(54, 57, 64);
		const ImVec4 pressed   = rgb(64, 68, 77);
		const ImVec4 border    = rgb(46, 48, 54);
		const ImVec4 accent    = rgb(240, 133, 58);
		const ImVec4 accentDim = rgb(240, 133, 58, 0.55f);

		auto& colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_Text]                  = rgb(225, 227, 231);
		colors[ImGuiCol_TextDisabled]          = rgb(124, 128, 137);
		colors[ImGuiCol_WindowBg]              = panel;
		colors[ImGuiCol_ChildBg]               = rgb(0, 0, 0, 0.0f);
		colors[ImGuiCol_PopupBg]               = raised;
		colors[ImGuiCol_Border]                = border;
		colors[ImGuiCol_BorderShadow]          = rgb(0, 0, 0, 0.0f);

		colors[ImGuiCol_FrameBg]               = field;
		colors[ImGuiCol_FrameBgHovered]        = hover;
		colors[ImGuiCol_FrameBgActive]         = pressed;

		colors[ImGuiCol_TitleBg]               = chrome;
		colors[ImGuiCol_TitleBgActive]         = chrome;
		colors[ImGuiCol_TitleBgCollapsed]      = chrome;
		colors[ImGuiCol_MenuBarBg]             = chrome;

		colors[ImGuiCol_ScrollbarBg]           = rgb(0, 0, 0, 0.0f);
		colors[ImGuiCol_ScrollbarGrab]         = rgb(70, 73, 81);
		colors[ImGuiCol_ScrollbarGrabHovered]  = rgb(88, 92, 101);
		colors[ImGuiCol_ScrollbarGrabActive]   = accentDim;

		colors[ImGuiCol_CheckMark]             = accent;
		colors[ImGuiCol_SliderGrab]            = accentDim;
		colors[ImGuiCol_SliderGrabActive]      = accent;

		colors[ImGuiCol_Button]                = field;
		colors[ImGuiCol_ButtonHovered]         = hover;
		colors[ImGuiCol_ButtonActive]          = pressed;

		// Headers: collapsing headers, selected rows/combo items, hovered menu items.
		colors[ImGuiCol_Header]                = rgb(48, 51, 58);
		colors[ImGuiCol_HeaderHovered]         = rgb(58, 62, 70);
		colors[ImGuiCol_HeaderActive]          = rgb(68, 72, 82);

		colors[ImGuiCol_Separator]             = border;
		colors[ImGuiCol_SeparatorHovered]      = accentDim;
		colors[ImGuiCol_SeparatorActive]       = accent;

		colors[ImGuiCol_ResizeGrip]            = rgb(0, 0, 0, 0.0f);
		colors[ImGuiCol_ResizeGripHovered]     = accentDim;
		colors[ImGuiCol_ResizeGripActive]      = accent;

		colors[ImGuiCol_Tab]                   = chrome;
		colors[ImGuiCol_TabHovered]            = hover;
		colors[ImGuiCol_TabActive]             = panel;
		colors[ImGuiCol_TabUnfocused]          = chrome;
		colors[ImGuiCol_TabUnfocusedActive]    = panel;

		colors[ImGuiCol_DockingPreview]        = rgb(240, 133, 58, 0.45f);
		colors[ImGuiCol_DockingEmptyBg]        = chrome;

		colors[ImGuiCol_PlotLines]             = rgb(160, 164, 172);
		colors[ImGuiCol_PlotLinesHovered]      = accent;
		colors[ImGuiCol_PlotHistogram]         = accentDim;
		colors[ImGuiCol_PlotHistogramHovered]  = accent;

		colors[ImGuiCol_TableHeaderBg]         = raised;
		colors[ImGuiCol_TableBorderStrong]     = border;
		colors[ImGuiCol_TableBorderLight]      = rgb(40, 42, 47);
		colors[ImGuiCol_TableRowBg]            = rgb(0, 0, 0, 0.0f);
		colors[ImGuiCol_TableRowBgAlt]         = rgb(255, 255, 255, 0.025f);

		colors[ImGuiCol_TextSelectedBg]        = rgb(240, 133, 58, 0.35f);
		colors[ImGuiCol_DragDropTarget]        = accent;
		colors[ImGuiCol_NavHighlight]          = accent;
		colors[ImGuiCol_NavWindowingHighlight] = rgb(255, 255, 255, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg]     = rgb(0, 0, 0, 0.45f);
		colors[ImGuiCol_ModalWindowDimBg]      = rgb(0, 0, 0, 0.55f);
	}
}