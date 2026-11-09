#include <oxide/tools/fps.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace oxide {

namespace {

bool terminalOutput() {
	static const bool enabled = [] {
		if (std::getenv("NO_COLOR") != nullptr) {
			return false;
		}
#if defined(_WIN32)
		return ::_isatty(::_fileno(stdout)) != 0;
#else
		return ::isatty(::fileno(stdout)) != 0;
#endif
	}();
	return enabled;
}

} // namespace

FPSCounter::FPSCounter(double start_time) : report_time_(start_time) {}

void FPSCounter::update(double current_time) {
	++frames_since_report_;
	const double elapsed = current_time - report_time_;
	if (elapsed < 1.0) {
		return;
	}

	const double fps = static_cast<double>(frames_since_report_) / elapsed;
	if (terminalOutput()) {
		std::printf("\r\033[KFPS: %.1f", fps);
	} else {
		std::printf("FPS: %.1f\n", fps);
	}
	std::fflush(stdout);

	frames_since_report_ = 0;
	report_time_         = current_time;
}

void FPSCounter::finish() const {
	if (terminalOutput()) {
		std::printf("\n");
	}
}

} // namespace oxide
