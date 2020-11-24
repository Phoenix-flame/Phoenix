#include "application.h"

#include "timestep.h"

Application::Application(const std::string name) {
    this->_name = name;
    // Create an instance of window*
    this->_window = std::make_shared<Window>(WindowProperties(this->_name));
    // Window Event Handler
    // Init Renderer
}


Application::~Application(){
    // Shutdown renderer
}


void Application::Run(){
    while(this->_running){
        float time = 0; //(float)glfwGetTime();
        Timestep timestep = time - _lastFrameTime;
        _lastFrameTime = time;


        if (!this->_minimized){
            {
                // for (Layer* layer : m_LayerStack)
                //     layer->OnUpdate(timestep);
            }

            // m_ImGuiLayer->Begin();
            // {
            //     HZ_PROFILE_SCOPE("LayerStack OnImGuiRender");

            //     for (Layer* layer : m_LayerStack)
            //         layer->OnImGuiRender();
            // }
            // m_ImGuiLayer->End();
        }

        // m_Window->OnUpdate();
    }   
}