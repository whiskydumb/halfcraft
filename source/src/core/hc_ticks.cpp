#include "hc_ticks.h"

#include <algorithm>
#include <cmath>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace halfcraft
{
	std::int64_t qpc_now()
	{
		LARGE_INTEGER now;
		::QueryPerformanceCounter(&now);
		return now.QuadPart;
	}

	void TickInterpolator::reset()
	{
		history_.clear();
		last_frame_qpc_ = 0;
		stamp_outliers_ = 0;
		render_delay_ms_ = 10.0;
		tick_due_init_ = false;
		tick_due_next_ = 0;
	}

	McPose TickInterpolator::sample(const proto::McState& mc, std::int64_t now_qpc)
	{
		McPose pose{ { mc.x, mc.y, mc.z }, { mc.eyeX, mc.eyeY, mc.eyeZ }, mc.bobPhase, mc.bobAmount };
		if (mc.tickQpc == 0 || mc.tickMs <= 0.0f) {
			return pose;
		}

		static const std::int64_t qpc_freq = [] {
			LARGE_INTEGER f;
			::QueryPerformanceFrequency(&f);
			return f.QuadPart;
		}();
		const double       qpc_per_ms = double(qpc_freq) / 1000.0;
		const std::int64_t period = std::max<std::int64_t>(1, std::llround(double(mc.tickMs) * qpc_per_ms));

		if (history_.empty() || history_.back().state.tickQpc != mc.tickQpc) {
			// minecraft restarted, or moved its player by itself (a pearl, /tp): the feet go straight to the
			// new spot, never through the walls between the two
			const bool restarted = !history_.empty() && mc.tickQpc < history_.back().state.tickQpc;
			const bool teleported = !history_.empty() && mc.teleportCount != history_.back().state.teleportCount;
			if (restarted || teleported) {
				history_.clear();
			}
			Tick tick{ mc, mc.tickQpc, 1 };
			if (!history_.empty()) {
				auto&              last = history_.back();
				const std::int64_t n = std::llround(double(mc.tickQpc - last.at) / double(period));
				const std::int64_t err = mc.tickQpc - (last.at + n * period);
				if (n == 0 && last.slots >= 2) {
					// minecraft ran two ticks in one frame and we saw both: the first one carries the
					// second's stamp. it belongs a tick earlier.
					last.at -= period;
					last.slots -= 1;
					tick.at = last.at + period;
				} else if (n >= 1 && n <= 10 && std::llabs(err) < period * 3 / 10) {
					tick.at = last.at + n * period + err / 16;  // the rhythm is exact; the stamps are noisy
					tick.slots = static_cast<int>(n);
					stamp_outliers_ = 0;
				} else if (n <= 10 && ++stamp_outliers_ < 3) {
					tick.slots = static_cast<int>(std::max<std::int64_t>(n, 1));
					tick.at = last.at + tick.slots * period;  // one odd stamp (a hitch): keep the rhythm
				} else {
					stamp_outliers_ = 0;  // lost the rhythm (a pause, a new tick rate): start from this stamp
				}
			}
			// was it already due on our last frame? then rendering had to wait for it.
			if (last_frame_qpc_ != 0) {
				if (!tick_due_init_) {
					tick_due_.fill(render_delay_ms_ - 1.0);
					tick_due_init_ = true;
				}
				const double due_ms = double(last_frame_qpc_ - tick.at) / qpc_per_ms;
				if (due_ms < 30.0) {  // later than that is a hitch, not a pattern to wait for
					tick_due_[tick_due_next_++ % tick_due_.size()] = due_ms;
				}
			}
			history_.push_back(tick);
			if (history_.size() > 8) {
				history_.pop_front();
			}
		}

		// the render delay follows how late ticks have been over the last 2 s: it grows 2% slower than
		// real time and shrinks 0.2% faster, too little to see either way.
		const double frame_ms = last_frame_qpc_ != 0 ? double(now_qpc - last_frame_qpc_) / qpc_per_ms : 0.0;
		last_frame_qpc_ = now_qpc;
		if (tick_due_init_) {
			const double target = std::clamp(*std::max_element(tick_due_.begin(), tick_due_.end()) + 1.0, 4.0, 30.0);
			const double dt = std::min(frame_ms, 100.0) / 1000.0;
			render_delay_ms_ = target > render_delay_ms_ ? std::min(target, render_delay_ms_ + 20.0 * dt) : std::max(target, render_delay_ms_ - 2.0 * dt);
		}
		const std::int64_t render_qpc = now_qpc - std::llround(render_delay_ms_ * qpc_per_ms);

		// the feet go through each tick's start (prev) and end (cur) positions.
		std::size_t i = 0;
		for (std::size_t k = history_.size(); k-- > 0;) {
			if (history_[k].at <= render_qpc) {
				i = k;
				break;
			}
		}
		const Tick&  tick = history_[i];
		const Tick*  next = i + 1 < history_.size() ? &history_[i + 1] : nullptr;
		const double ticks = double(render_qpc - tick.at) / double(period);
		const double t = std::clamp(ticks, 0.0, 1.0);
		const auto&  s = tick.state;
		pose.feet[0] = s.prevX + (s.curX - s.prevX) * t;
		pose.feet[1] = s.prevY + (s.curY - s.prevY) * t;
		pose.feet[2] = s.prevZ + (s.curZ - s.prevZ) * t;
		double eye_height = s.tickEyeO + (s.tickEye - s.tickEyeO) * t;
		pose.bob_phase = -(s.walkDist + (s.walkDist - s.walkDistO) * static_cast<float>(t));
		pose.bob_amount = s.bobO + (s.bob - s.bobO) * static_cast<float>(t);
		if (ticks > 1.0 && next) {
			// past this tick's end, and the next tick we have starts later: minecraft ran one we never
			// saw. carry on from this tick's end to the next one's start.
			const auto&  n = next->state;
			const double gap = double(next->at - (tick.at + period));
			const double u = gap > 0.0 ? std::clamp(double(render_qpc - (tick.at + period)) / gap, 0.0, 1.0) : 1.0;
			pose.feet[0] = s.curX + (n.prevX - s.curX) * u;
			pose.feet[1] = s.curY + (n.prevY - s.curY) * u;
			pose.feet[2] = s.curZ + (n.prevZ - s.curZ) * u;
			eye_height = s.tickEye + (n.tickEyeO - s.tickEye) * u;
			const float end_phase = -(s.walkDist + (s.walkDist - s.walkDistO));
			pose.bob_phase = end_phase + (-n.walkDist - end_phase) * static_cast<float>(u);
			pose.bob_amount = s.bob + (n.bobO - s.bob) * static_cast<float>(u);
		}
		pose.eye[0] = pose.feet[0];
		pose.eye[1] = pose.feet[1] + eye_height;
		pose.eye[2] = pose.feet[2];
		return pose;
	}
}
