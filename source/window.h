#include <string>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <math.h>
#include <memory>
#include <function>


struct WindowProperties{
    WindowProperties(std::string name){
        this->title = name;
        this->width = 800;
        this->height = 600;
    }
    std::string title;
    int width;
    int height;
};


class Window{
public:
    Window(const WindowProperties& cfg);
    virtual ~Window();

    void OnUpdate();

    unsigned int GetWidth() const { return m_Data.Width; }
    unsigned int GetHeight() const { return m_Data.Height; }

    void SetEventCallback(const std::function<void(Event&)& callback) { m_Data.EventCallback = callback; }
    void SetVSync(bool enabled);
    bool IsVSync() const;

    virtual void* GetNativeWindow() const { return _window; }

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
        std::function<void(Event&) EventCallback;
    };

    WindowData m_Data;
};