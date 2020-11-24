#ifndef __BASE__
#define __BASE__
#include <iostream>
#include "log.h"

#define BIT(x) (1 << x)
#define BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }





#endif