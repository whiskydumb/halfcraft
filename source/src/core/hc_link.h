#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "halfcraft_protocol.h"

// the shared-memory link to minecraft (protocol/halfcraft_protocol.h). the host (source) creates
// the mapping, minecraft opens it.
//
// client.dll and server.dll each map it. every region has exactly one writer and one reader:
//   client.dll - header/heartbeat, host state, water grid, water probes, input ring, overlay, world entities,
//                render ring, host debug (HostDebug)
//   server.dll - collision ring, actor table, event ring, weapon table, mob table, host debug
//                (HostDebugServer) (it also reads the header and both states)
// only client.dll resets the mapping (create()); server.dll just attaches (attach()), and only to
// what it uses: in half-life 2's 32-bit process both dlls share 2 GB of address space, and the
// overlay and the render ring (159 MB of the mapping's 191) are the client's alone.

namespace halfcraft
{

	class Link
	{
	public:
		Link() = default;
		~Link();
		Link(const Link&) = delete;
		Link& operator=(const Link&) = delete;

		/// creates (or reuses) the mapping and resets every region the host owns. client.dll only.
		/// @return true when the mapping is usable
		bool create();
		/// maps the same memory without resetting anything, up to the end of the collision ring: the
		/// overlay and the render ring stay unmapped. server.dll only.
		/// @return true when the mapping is usable
		bool               attach();
		[[nodiscard]] bool valid() const { return base_ != nullptr; }

		/// minecraft wrote its heartbeat within the last few seconds.
		[[nodiscard]] bool mc_alive() const;
		/// process id minecraft wrote when it opened the mapping (changes when it restarts).
		[[nodiscard]] std::uint32_t mc_pid() const;
		void                        heartbeat();

		// host <-> minecraft state (seqlocks)
		void write_host_state(const proto::HostState& state);
		bool read_host_state(proto::HostState& out) const;
		bool read_mc_state(proto::McState& out) const;
		void write_water_grid(const proto::WaterGrid& grid);
		/// where minecraft wants the host's water probed beyond the water grid (seqlock); out.count says how many.
		bool read_water_probe_requests(proto::WaterProbeRequests& out) const;
		void write_water_probes(const proto::WaterProbes& probes);
		/// (#7) what minecraft's debug screen shows: client.dll writes HostDebug, server.dll HostDebugServer.
		void write_host_debug(const proto::HostDebug& debug);
		void write_host_debug_server(const proto::HostDebugServer& debug);

		/// how far behind each ring's reader is, right now.
		struct Backlog
		{
			std::uint64_t input = 0;   // entries
			std::uint64_t events = 0;  // entries
			std::uint64_t collision_bytes = 0;
			std::uint64_t render_bytes = 0;  // 0 past an attach()ed view
		};
		[[nodiscard]] Backlog backlog() const;

		/// input ring, producer side. drops the event if minecraft is a whole ring behind.
		void push_input(proto::InputType type, std::uint16_t code, std::int32_t a = 0, std::int32_t b = 0, std::int32_t c = 0);
		/// a whole string for one of minecraft's string channels, in kInString pieces (input ring producer).
		/// @return false, sending nothing, when the ring has no room for all of it
		bool push_string(proto::StringChannel channel, std::string_view utf8);

		/// collision ring, producer side (one thread only).
		/// @return false when the ring is full
		bool write_collision(proto::ColType type, const void* payload, std::uint32_t bytes);

		void write_actors(const proto::ActorRecord* records, std::uint32_t count);
		/// (#12) the player's weapons minecraft shows as items.
		void write_weapons(const proto::WeaponTable& table);
		/// (#12) the weapon table as server.dll wrote it. client.dll reads it as well as minecraft: it
		/// holds the weapon source really has out, where the client's own is predicted.
		bool read_weapons(proto::WeaponTable& out) const;
		/// event ring, consumer side.
		/// @return false when there is nothing to pop
		bool pop_event(proto::McEvent& out);
		bool read_world_entities(proto::WorldEntities& out) const;
		/// minecraft's mobs near its player (seqlock); out.count says how many records were read.
		bool read_mobs(proto::MobTable& out) const;

		/// render ring, consumer side: calls fn(type, payload, bytes) per pending message, up to
		/// about max_bytes of payload. payload points into shared memory.
		void drain_render(const std::function<void(std::uint32_t, const std::uint8_t*, std::uint32_t)>& fn, std::uint64_t max_bytes);

		/// overlay triple buffer, consumer side. swaps a newer frame (if any) into the front slot.
		/// @return true when the front slot changed
		bool acquire_overlay_frame();
		/// minecraft (re)connected: its writer starts over, so the swap does too.
		void                                       reset_overlay();
		[[nodiscard]] const std::uint8_t*          front_pixels() const;
		[[nodiscard]] const proto::OverlaySlotHdr* front_header() const;

	private:
		/// @param view_bytes - how much of the mapping, from its start, this process maps
		bool map(bool reset, std::uint64_t view_bytes);
		/// a ring minecraft writes: our consumer index catches up with its producer index.
		void skip_pending(std::uint64_t head_offset, std::uint64_t tail_offset);
		/// the view reaches up to end (server.dll's stops before the overlay).
		[[nodiscard]] bool maps(std::uint64_t end) const { return base_ && end <= view_bytes_; }

		template <class T>
		T* at(std::uint64_t offset) const
		{
			return reinterpret_cast<T*>(base_ + offset);
		}

		void*         mapping_{ nullptr };
		std::uint8_t* base_{ nullptr };
		std::uint64_t view_bytes_{ 0 };
		std::uint32_t overlay_front_{ 2 };
	};
}
