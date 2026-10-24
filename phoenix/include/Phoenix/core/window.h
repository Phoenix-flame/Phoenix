#include <string>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <math.h>
#include <memory>
#include <functional>

#include "Phoenix/event/event.h"
#include "Phoenix/event/applicationEvent.h"
#include "Phoenix/event/keyEvent.h"
#include "Phoenix/event/mouseEvent.h"

namespace Phoenix{
    struct WindowProperties{
        WindowProperties(std::string name, int width = 1280, int height = 720, bool customTitleBar = false){
            this->title = name;
            this->width = width;
            this->height = height;
            this->customTitleBar = customTitleBar;
        }
        std::string title;
        int width;
        int height;
        // Undecorated OS window: the app draws its own title bar (drag, min/max/close)
        // and resize edges, using the window-control methods below.
        bool customTitleBar;
    };


    class Window{
    public:
        Window(const WindowProperties& cfg);
        virtual ~Window();

        void OnUpdate();

        unsigned int GetWidth() const { return _data.Width; }
        unsigned int GetHeight() const { return _data.Height; }

        void SetEventCallback(const std::function<void(Event&)>& callback) { _data.EventCallback = callback; }
        void SetVSync(bool enabled);
        bool IsVSync() const;

        virtual void* GetNativeWindow() const { return _window; }

        // Window controls for apps that draw their own title bar.
        bool HasCustomTitleBar() const { return _data.CustomTitleBar; }
        void Minimize();
        void Maximize();
        void Restore();
        bool IsMaximized() const;
        glm::ivec2 GetPosition() const;
        void SetPosition(int x, int y);
        void SetSize(int width, int height);
        // Cursor position in desktop coordinates (window position + cursor offset).
        glm::vec2 GetCursorScreenPosition() const;
        // True when a desktop point is at the top edge of its monitor's usable area
        // (or above it, e.g. over a panel). Used to snap-maximize on title-bar drags.
        bool IsAtMonitorTop(const glm::vec2& screenPos, float margin = 2.0f) const;

    private:
        virtual void Init(const WindowProperties& props);
        virtual void Shutdown();
    private:
        GLFWwindow* _window;
        // std::unique_ptr<GraphicsContext> m_Context;

        struct WindowData
        {
            std::string Title;
            unsigned int Width, Height;
            bool VSync;
            bool CustomTitleBar = false;
            std::function<void(Event&)> EventCallback;
        };

        WindowData _data;
    };
}

