#pragma once

#include <array>
#include <cstdint>
#include <deque>

#include "halfcraft_protocol.h"

// minecraft's 20 Hz physics ticks, interpolated on source's frame clock (what minecraft's own
// renderer does with partial ticks). sampling minecraft's per-frame position instead judders,
// because the two games' frames are not phase-locked. ported from SkyCraft's Game.cpp.
//
// minecraft ticks exactly every tickMs but stamps a tick only after that frame's network work, and
// we see it on our next frame. so tick times are locked to that rhythm (stamp noise filtered out)
// and we render just far enough in the past that the next tick has always arrived.

namespace halfcraft
{

	/// QueryPerformanceCounter now (the clock minecraft stamps its ticks with).
	std::int64_t qpc_now();

	struct McPose
	{
		double feet[3];     // minecraft coords
		double eye[3];      // minecraft coords (eye height interpolated too)
		float  bob_phase;   // minecraft's walk-bob inputs at this instant
		float  bob_amount;
	};

	class TickInterpolator
	{
	public:
		/// @param mc - the latest minecraft state (read this frame)
		/// @param now_qpc - QueryPerformanceCounter now
		/// @return where minecraft's player is at our render time
		McPose sample(const proto::McState& mc, std::int64_t now_qpc);
		void   reset();

	private:
		struct Tick
		{
			proto::McState state;
			std::int64_t   at;     // when it happened (qpc), on the locked rhythm
			int            slots;  // ticks since the one before it (2+: we missed one)
		};

		std::deque<Tick>       history_;
		std::int64_t           last_frame_qpc_ = 0;
		int                    stamp_outliers_ = 0;
		double                 render_delay_ms_ = 10.0;
		std::array<double, 40> tick_due_{};  // last 2 s: how long each tick was already due when first seen
		std::size_t            tick_due_next_ = 0;
		bool                   tick_due_init_ = false;
	};
}
