#ifndef __APPLICATION__
#define __APPLICATION__

#include <Phoenix/imGui/ImGuiLayer.h>
#include <string>
#include "window.h"
#include "base.h"
#include "layerStack.h"
#include "timestep.h"
#include "log.h"
#include "assert.h"

int main(int argc, char** argv);
namespace Phoenix{
    class Application{
    public:
        Application(const std::string name = "Phoenix_flame");
        virtual ~Application();
        void OnEvent(Event&);
        void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);

		Window& GetWindow() { return *_window; }

		void Close();

		// ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; }
        static Application& Get() { return *s_Instance; }
    private:
        void Run();
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);

    private:
        std::string _name;
        bool _minimized = false;
        bool _running = true;
        float _lastFrameTime = 0.0f;
        std::unique_ptr<Window> _window;
        LayerStack _layerStack;
        ImGuiLayer* _imGuiLayer;

    private:
        static Application* s_Instance;
        friend int ::main(int argc, char** argv);
        
    };
}


#endif