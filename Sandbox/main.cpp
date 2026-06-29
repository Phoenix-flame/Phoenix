#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <limits.h>
#include <Phoenix/core/application.h>
#include "MainLayer.h"

// Make the working directory the executable's directory so all asset paths
// resolve relative to the binary, regardless of where it was launched from.
static void SetWorkingDirToExecutable(){
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len == -1) { return; }
    buffer[len] = '\0';
    std::string path(buffer);
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos){
        if (chdir(path.substr(0, slash).c_str()) != 0) { /* ignore */ }
    }
}

int main(int argc, char** argv){
    SetWorkingDirToExecutable();
    Log::Init();
    Application app;
    PHX_INFO("Hello, World!");
    app.PushLayer(new MainLayer("Main Layer"));
    app.Run();
}
