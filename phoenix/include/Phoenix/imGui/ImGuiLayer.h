#pragma once

#include <Phoenix/core/layer.h>

struct ImFont;

namespace Phoenix{
    class ImGuiLayer : public Layer{
	public:
		ImGuiLayer();
		~ImGuiLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnEvent(Event& e) override;

		void Begin();
		void End();

		void BlockEvents(bool block) { _blockEvents = block; }
		
		void SetDarkThemeColors();

		// Monospace font for code (a system mono font when one is found, else
		// ImGui's built-in ProggyClean). Valid after OnAttach.
		static ImFont* GetMonospaceFont() { return s_MonospaceFont; }
		// Larger fonts for in-game text (script HUD lines and banners), baked at the
		// size they're drawn so they stay sharp. Valid after OnAttach.
		static ImFont* GetHudFont() { return s_HudFont; }
		static ImFont* GetBannerFont() { return s_BannerFont; }
	private:
		static inline ImFont* s_MonospaceFont = nullptr;
		static inline ImFont* s_HudFont = nullptr;
		static inline ImFont* s_BannerFont = nullptr;
		bool _blockEvents = true;
	};
}