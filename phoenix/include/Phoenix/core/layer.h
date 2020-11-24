#ifndef __LAYER__
#define __LAYER__
#include "Phoenix/event/event.h"
#include "timestep.h"


namespace Phoenix{
    class Layer{
        public:
            Layer(const std::string& name = "Layer"): layer_name(name) {}
            virtual ~Layer() = default;

            virtual void OnAttach() {}
            virtual void OnDetach() {}
            virtual void OnImGuiRender() {}
            virtual void OnUpdate(Timestep ts) {}
            virtual void OnEvent(Event& e) {}
        protected:
            std::string layer_name;
    };
}

#endif