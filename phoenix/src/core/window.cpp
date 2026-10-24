#include <GL/glew.h>
#include "Phoenix/core/window.h"
#include <Phoenix/core/assert.h>
#include <algorithm>

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
        _data.CustomTitleBar = props.customTitleBar;

#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
        // A custom title bar moves the window itself (glfwSetWindowPos), which Wayland
        // doesn't allow. Run through XWayland instead when it is available.
        if (props.customTitleBar && glfwPlatformSupported(GLFW_PLATFORM_X11))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif

        int success = glfwInit();
        PHX_CORE_ASSERT(success, "Could not initialize GLFW!");
        glfwSetErrorCallback(GLFWErrorCallback);

#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
        // Still on Wayland: an undecorated window couldn't be dragged, so let the
        // compositor draw the frame.
        if (_data.CustomTitleBar && glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
            _data.CustomTitleBar = false;
#endif

        if (_data.CustomTitleBar)
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

        // Don't open larger than the desktop (minus a margin), e.g. on a small laptop screen.
        int width = props.width, height = props.height;
        if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()){
            int wx, wy, ww, wh;
            glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);
            if (ww > 0 && wh > 0){
                width = std::min(width, ww - 40);
                height = std::min(height, wh - 40);
            }
        }
        _data.Width = width;
        _data.Height = height;

        _window = glfwCreateWindow(width, height, props.title.c_str(), nullptr, nullptr);
        if (_data.CustomTitleBar)
            glfwSetWindowSizeLimits(_window, 640, 360, GLFW_DONT_CARE, GLFW_DONT_CARE);

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

    void Window::SetCursorLocked(bool locked){
        glfwSetInputMode(_window, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(_window, GLFW_RAW_MOUSE_MOTION, locked ? GLFW_TRUE : GLFW_FALSE);
    }

    bool Window::IsCursorLocked() const{
        return glfwGetInputMode(_window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    }

    bool Window::IsFocused() const{
        return glfwGetWindowAttrib(_window, GLFW_FOCUSED) == GLFW_TRUE;
    }

    void Window::Minimize(){
        glfwIconifyWindow(_window);
    }

    void Window::Maximize(){
        glfwMaximizeWindow(_window);
    }

    void Window::Restore(){
        glfwRestoreWindow(_window);
    }

    bool Window::IsMaximized() const{
        return glfwGetWindowAttrib(_window, GLFW_MAXIMIZED) == GLFW_TRUE;
    }

    glm::ivec2 Window::GetPosition() const{
        int x = 0, y = 0;
        glfwGetWindowPos(_window, &x, &y);
        return { x, y };
    }

    void Window::SetPosition(int x, int y){
        glfwSetWindowPos(_window, x, y);
    }

    void Window::SetSize(int width, int height){
        glfwSetWindowSize(_window, width, height);
    }

    bool Window::IsAtMonitorTop(const glm::vec2& p, float margin) const{
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; i++){
            int mx = 0, my = 0;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (!mode) { continue; }
            if (p.x < (float)mx || p.x >= (float)(mx + mode->width)) { continue; }
            int wx = 0, wy = 0, ww = 0, wh = 0;
            glfwGetMonitorWorkarea(monitors[i], &wx, &wy, &ww, &wh);
            return p.y <= (float)wy + margin;
        }
        return false;
    }

    glm::vec2 Window::GetCursorScreenPosition() const{
        double cx = 0.0, cy = 0.0;
        glfwGetCursorPos(_window, &cx, &cy);
        glm::ivec2 pos = GetPosition();
        return { (float)pos.x + (float)cx, (float)pos.y + (float)cy };
    }
}

