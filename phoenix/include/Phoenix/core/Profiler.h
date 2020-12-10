#pragma once
#include <chrono>
#include <Phoenix/core/application.h>

namespace Phoenix{
    class Profiler{
    public:
        Profiler(std::string name) : m_Name(name) {
            clock = std::chrono::high_resolution_clock::now();
        }
        ~Profiler(){
            auto end = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration_cast<std::chrono::microseconds>(end - clock).count();
            if (Application::s_TimeContainer.find(m_Name) == Application::s_TimeContainer.end()){
                Application::s_TimeContainer.insert(std::pair<std::string, int>(m_Name, time));
            }
            else {
                Application::s_TimeContainer[m_Name] = time;
            }
            
        }
    private:
        std::string m_Name;
        std::chrono::system_clock::time_point clock;
    };


    #define PHX_PROFILE(name) Profiler p(name);


}