#ifndef __ASSERT__
#define __ASSERT__
#include <filesystem>
#include <Phoenix/core/log.h>

// Helpers the assert macros rely on (they were never defined, so enabling
// PHX_ENABLE_ASSERTS failed to compile).
#define PHX_EXPAND_MACRO(x) x
#define PHX_STRINGIFY_MACRO(x) #x
#if defined(_MSC_VER)
	#define PHX_DEBUGBREAK() __debugbreak()
#else
	#include <csignal>
	#define PHX_DEBUGBREAK() std::raise(SIGTRAP)
#endif

// Enabled in Debug builds (see phoenix/CMakeLists.txt).
#ifdef PHX_ENABLE_ASSERTS

	// Alteratively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
	// provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
	#define PHX_INTERNAL_ASSERT_IMPL(type, check, msg, ...) { if(!(check)) { PHX##type##ERROR(msg, __VA_ARGS__); PHX_DEBUGBREAK(); } }
	#define PHX_INTERNAL_ASSERT_WITH_MSG(type, check, ...) PHX_INTERNAL_ASSERT_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
	#define PHX_INTERNAL_ASSERT_NO_MSG(type, check) PHX_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", PHX_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

	#define PHX_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
	#define PHX_INTERNAL_ASSERT_GET_MACRO(...) PHX_EXPAND_MACRO( PHX_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, PHX_INTERNAL_ASSERT_WITH_MSG, PHX_INTERNAL_ASSERT_NO_MSG) )

	// Currently accepts at least the condition and one additional parameter (the message) being optional
	#define PHX_ASSERT(...) PHX_EXPAND_MACRO( PHX_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
	#define PHX_CORE_ASSERT(...) PHX_EXPAND_MACRO( PHX_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
	#define PHX_ASSERT(...)
	#define PHX_CORE_ASSERT(...)
#endif



#endif