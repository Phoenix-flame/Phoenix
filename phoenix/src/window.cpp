#include <GL/glew.h>
#include "Phoenix/core/window.h"
#include <Phoenix/core/assert.h>

static void GLFWErrorCallback(int error, const char* description){
   std::cerr << "[" << error << "]: " << description << std::endl; 
}

namespace Phoenix{
    Window::Window(const WindowProperties& props){
        Init(props);
    }

    Window::~Window(){
        Shutdown();
    }

    void Window::Init(const WindowProperties& props){
        _data.Title = props.title;
        _data.Width = props.width;
        _data.Height = props.height;


        int success = glfwInit();
        PHX_CORE_ASSERT(success, "Could not initialize GLFW!");
        glfwSetErrorCallback(GLFWErrorCallback);

        _window = glfwCreateWindow((int)props.width, (int)props.height, props.title.c_str(), nullptr, nullptr);

        glfwMakeContextCurrent(_window);

        glewInit();
        glfwSetWindowUserPointer(_window, &_data);
        SetVSync(false);

        // // Set GLFW callbacks
        glfwSetWindowSizeCallback(_window, [](GLFWwindow* window, int width, int height){
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            data.Width = width;
            data.Height = height;

            WindowResizeEvent event(width, height);
            data.EventCallback(event);
        });

        glfwSetWindowCloseCallback(_window, [](GLFWwindow* window){
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            WindowCloseEvent event;
            data.EventCallback(event);
        });

        glfwSetKeyCallback(_window, [](GLFWwindow* window, int key, int scancode, int action, int mods){
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            switch (action){
                case GLFW_PRESS:{
                    KeyPressedEvent event(key, 0);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE:{
                    KeyReleasedEvent event(key);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_REPEAT:{
                    KeyPressedEvent event(key, 1);
                    data.EventCallback(event);
                    break;
                }
            }
        });

        glfwSetCharCallback(_window, [](GLFWwindow* window, unsigned int keycode){
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            KeyTypedEvent event(keycode);
            data.EventCallback(event);
        });

        glfwSetMouseButtonCallback(_window, [](GLFWwindow* window, int button, int action, int mods){
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            switch (action){
                case GLFW_PRESS:{
                    MouseButtonPressedEvent event(button);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE:{
                    MouseButtonReleasedEvent event(button);
                    data.EventCallback(event);
                    break;
                }
            }
        });

        glfwSetScrollCallback(_window, [](GLFWwindow* window, double xOffset, double yOffset){
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            MouseScrolledEvent event((float)xOffset, (float)yOffset);
            data.EventCallback(event);
        });

        glfwSetCursorPosCallback(_window, [](GLFWwindow* window, double xPos, double yPos){
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            MouseMovedEvent event((float)xPos, (float)yPos);
            data.EventCallback(event);
        });
    }


    void Window::Shutdown(){
        glfwDestroyWindow(_window);
        glfwTerminate();
    }

    void Window::OnUpdate(){
        glfwSwapBuffers(_window);
        glfwPollEvents();
    }

    void Window::SetVSync(bool enabled){
        if (enabled)
            glfwSwapInterval(1);
        else
            glfwSwapInterval(0);

        _data.VSync = enabled;
    }

    bool Window::IsVSync() const{
        return _data.VSync;
    }
}

