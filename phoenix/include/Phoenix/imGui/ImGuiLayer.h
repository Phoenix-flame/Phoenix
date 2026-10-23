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

		// Fonts loaded in OnAttach, in atlas order (Bold stays index 0 for existing users).
		enum class FontType { Bold = 0, Regular = 1, Heading = 2, Small = 3 };
		static ImFont* GetFont(FontType type);
	private:
		bool _blockEvents = true;
	};
}