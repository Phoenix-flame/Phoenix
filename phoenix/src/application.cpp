#include "Phoenix/core/application.h"



namespace Phoenix{

    Application::Application(const std::string name) {
        this->_name = name;
        Log::Init();
        this->_window = std::make_unique<Window>(WindowProperties(this->_name));
        this->_window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
        // Init Renderer

        glViewport(0, 0, this->_window->GetWidth(), this->_window->GetHeight());

        PHX_CORE_INFO("Application initialized successfully.");
    }

    Application::~Application(){
        // Shutdown renderer
    }

    void Application::OnEvent(Event& e){
        EventDispatcher dispathcer(e);
        dispathcer.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));
        dispathcer.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
    }

    void Application::PushLayer(Layer* layer){
		_layerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer){
		_layerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::Close(){
		_running = false;
	}


    void Application::Run(){
        while(this->_running){
            float time = (float)glfwGetTime();
            Timestep timestep = time - _lastFrameTime;
            _lastFrameTime = time;


            if (!this->_minimized){
                {
                    for (Layer* layer : _layerStack){
                        layer->OnUpdate(timestep);
                    }  
                }

                // m_ImGuiLayer->Begin();
                // {
                //     HZ_PROFILE_SCOPE("LayerStack OnImGuiRender");

                //     for (Layer* layer : m_LayerStack)
                //         layer->OnImGuiRender();
                // }
                // m_ImGuiLayer->End();
            }

            _window->OnUpdate();
        }   
    }


    bool Application::OnWindowClose(WindowCloseEvent& e){
        _running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e){
        if (e.GetWidth() == 0 || e.GetHeight() == 0){
            _minimized = true;
            return false;
        }

        _minimized = false;
        // Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

        return false;
    }
}