#include <iostream>
#include <memory>
#include <Phoenix/core/application.h>
#include "ExampleLayer.h"
int main(int argc, char** argv){
    Log::Init();
    Application app;
    PHX_INFO("Hello, World!");
    app.PushLayer(new ExampleLayer("Example Layer"));
    app.Run();
}
