#include <string>
#include <unistd.h>
#include <limits.h>
#include <Phoenix/core/application.h>
#include "ExampleLayer.h"

// Assets are copied next to the executable (see phoenix_copy_assets), and the engine
// loads them by relative path, so run from the executable's directory.
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
    Application app("Phoenix Example");
    app.PushLayer(new ExampleLayer("Example Layer"));
    app.Run();
}
