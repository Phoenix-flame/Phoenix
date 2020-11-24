#include <iostream>
#include <memory>
#include <core/application.h>

int main(int argc, char** argv){
    Phoenix::Application app;
    PHX_INFO("Hello, World!");
    app.Run();
}
