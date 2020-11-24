#include "core.h"


Core::Core(){

}
Core::~Core(){
    glfwTerminate();
}
void Core::Init(){
    if (!glfwInit()){
        throw std::runtime_error("GLFW initialization failed.");
    }
    window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Phoenix_flame", NULL, NULL);
    if (!window){
        glfwTerminate();
        throw std::runtime_error("Window initialization failed.");
    }
    glfwMakeContextCurrent(window);
}


void Core::Render(){
    while (!glfwWindowShouldClose(window)){
        float currentFrame = glfwGetTime();
        glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}