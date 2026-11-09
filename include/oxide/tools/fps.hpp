#pragma once

#include <cstdint>

namespace oxide {

// ============================================================================
// Frame-rate reporting
// ============================================================================
class FPSCounter {
  public:
	explicit FPSCounter(double start_time);

	void update(double current_time);
	void finish() const;

  private:
	std::uint64_t frames_since_report_ = 0;
	double        report_time_;
};

} // namespace oxide
