#pragma once

#include <oxide/oxide_config.hpp>

#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oxide {

enum class LogLevel {
	Info,
	Warning,
	Error,
	Fatal,
};

// Write one diagnostic line to stderr in the form:
//   [oxide][ERROR] buffer.cpp:24: failed to create buffer
//
// Passing an empty source_location (for example for messages that originate
// in the Vulkan driver rather than in this codebase) omits the file/line
// prefix and leaves only the source label and message.
void logMessage(
    LogLevel                    level,
    std::string_view            message,
    const std::source_location &location = std::source_location::current());

// Captures a stream expression at the call site so every diagnostic reports
// exactly where it came from:
//   OXIDE_LOG_ERROR << "failed to create buffer (" << result << ")";
class LogLine {
  public:
	LogLine(LogLevel level, const std::source_location &location)
	    : level_(level), location_(location) {}
	~LogLine();

	LogLine(const LogLine &)            = delete;
	LogLine &operator=(const LogLine &) = delete;

	template <typename T>
	LogLine &operator<<(const T &value) {
		stream_ << value;
		return *this;
	}

  private:
	LogLevel             level_;
	std::source_location location_;
	std::ostringstream   stream_;
};

// Prefix an exception message with the source location that raised it.
[[nodiscard]] std::string locatedMessage(
    std::string_view            message,
    const std::source_location &location);

// Exception factories that capture the throw site automatically:
//   throw runtimeError("failed to create swapchain");
[[nodiscard]] std::runtime_error runtimeError(
    std::string                 message,
    const std::source_location &location = std::source_location::current());
[[nodiscard]] std::invalid_argument invalidArgument(
    std::string                 message,
    const std::source_location &location = std::source_location::current());
[[nodiscard]] std::out_of_range outOfRange(
    std::string                 message,
    const std::source_location &location = std::source_location::current());

#if !OXIDE_ENABLE_LOGGING
// Stream sink used by the logging macros in builds where diagnostics are
// compiled out. It accepts any stream expression and discards it.
class NullLogLine {
  public:
	template <typename T>
	NullLogLine &operator<<(const T &) noexcept {
		return *this;
	}
};
#endif

} // namespace oxide

#if OXIDE_ENABLE_LOGGING
#define OXIDE_LOG_INFO \
	(::oxide::LogLine(::oxide::LogLevel::Info, ::std::source_location::current()))
#define OXIDE_LOG_WARNING \
	(::oxide::LogLine(::oxide::LogLevel::Warning, ::std::source_location::current()))
#define OXIDE_LOG_ERROR \
	(::oxide::LogLine(::oxide::LogLevel::Error, ::std::source_location::current()))
#define OXIDE_LOG_FATAL \
	(::oxide::LogLine(::oxide::LogLevel::Fatal, ::std::source_location::current()))
#else
#define OXIDE_LOG_INFO (::oxide::NullLogLine{})
#define OXIDE_LOG_WARNING (::oxide::NullLogLine{})
#define OXIDE_LOG_ERROR (::oxide::NullLogLine{})
#define OXIDE_LOG_FATAL (::oxide::NullLogLine{})
#endif
