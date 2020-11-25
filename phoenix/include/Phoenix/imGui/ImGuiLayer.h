#pragma once

#include <Phoenix/core/layer.h>

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
	private:
		bool _blockEvents = true;
	};
}