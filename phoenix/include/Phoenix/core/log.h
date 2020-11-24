#ifndef __LOG__ 
#define __LOG__
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <memory>

namespace Phoenix {

	class Log{
	public:
		static void Init();

		static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
	};

}

// Core log macros
#define PHX_CORE_TRACE(...)    ::Phoenix::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define PHX_CORE_INFO(...)     ::Phoenix::Log::GetCoreLogger()->info(__VA_ARGS__)
#define PHX_CORE_WARN(...)     ::Phoenix::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define PHX_CORE_ERROR(...)    ::Phoenix::Log::GetCoreLogger()->error(__VA_ARGS__)
#define PHX_CORE_CRITICAL(...) ::Phoenix::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client log macros
#define PHX_TRACE(...)         ::Phoenix::Log::GetClientLogger()->trace(__VA_ARGS__)
#define PHX_INFO(...)          ::Phoenix::Log::GetClientLogger()->info(__VA_ARGS__)
#define PHX_WARN(...)          ::Phoenix::Log::GetClientLogger()->warn(__VA_ARGS__)
#define PHX_ERROR(...)         ::Phoenix::Log::GetClientLogger()->error(__VA_ARGS__)
#define PHX_CRITICAL(...)      ::Phoenix::Log::GetClientLogger()->critical(__VA_ARGS__)


#endif