#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <math.h>
#include <memory>

class Core{
public:
    Core();
    ~Core();
    void Init();
    void Render();

private:
    GLFWwindow* window;
    int SCR_WIDTH = 800;
    int SCR_HEIGHT = 600;
};
