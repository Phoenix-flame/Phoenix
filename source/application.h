#include <string>
#include "window.h"

class Application{
public:
    Application(const std::string name = "Phoenix_flame");
    virtual ~Application();
private:
    void Run();
    void onEvent();
    friend int ::main(int argc, char** argv);

private:
    std::string _name;
    bool _minimized = false;
    bool _running = true;
    float _lastFrameTime = 0.0f;
    std::shared_ptr<Window> _window;
    
};