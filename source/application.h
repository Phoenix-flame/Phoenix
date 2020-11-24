#include <string>
#include "window.h"
#include "base.h"


int main(int argc, char** argv);

class Application{
public:
    Application(const std::string name = "Phoenix_flame");
    virtual ~Application();
    void onEvent(Event&);


private:
    void Run();
    bool OnWindowClose(WindowCloseEvent& e);
    bool OnWindowResize(WindowResizeEvent& e);
    friend int ::main(int argc, char** argv);

private:
    std::string _name;
    bool _minimized = false;
    bool _running = true;
    float _lastFrameTime = 0.0f;
    std::shared_ptr<Window> _window;
    
};