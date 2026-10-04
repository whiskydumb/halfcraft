#pragma once

// engine-agnostic logging for the halfcraft core. the host (client.dll / server.dll) points it at
// the source console; until then lines go to the debugger.

namespace halfcraft
{
	inline constexpr int LOG_INFO = 0;
	inline constexpr int LOG_WARNING = 1;
	inline constexpr int LOG_ERROR = 2;

	/// receives every finished log line.
	/// @param severity - LOG_INFO, LOG_WARNING or LOG_ERROR
	/// @param line - the formatted line, without a trailing newline
	using LogSink = void (*)(int severity, const char* line);

	void set_log_sink(LogSink sink);

	void log_info(const char* format, ...);
	void log_warning(const char* format, ...);
	void log_error(const char* format, ...);
}
