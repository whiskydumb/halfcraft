#include "hc_link.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <intrin.h>

#include "hc_atomic.h"
#include "hc_log.h"

#pragma comment(lib, "advapi32.lib")

namespace halfcraft
{
	namespace
	{
		constexpr std::uint64_t MC_TIMEOUT_MS = 3000;

		// server.dll's view of the mapping: from the start up to the end of the collision ring, the
		// last region it uses. 32 MB instead of the 191 MB client.dll maps in the same process
		constexpr std::uint64_t SERVER_VIEW_BYTES = proto::kOffCollisionRing + proto::kCollisionRingBytes;
		static_assert(proto::kOffMcState + sizeof(proto::McState) <= SERVER_VIEW_BYTES, "server.dll reads minecraft's state");
		static_assert(proto::kOffActorTable + sizeof(proto::ActorTable) <= SERVER_VIEW_BYTES, "server.dll writes the actor table");
		static_assert(proto::kOffEventRing + proto::kEventRingDataOff + sizeof(proto::McEvent) * proto::kEventRingEntries <= SERVER_VIEW_BYTES,
			"server.dll reads the event ring");
		static_assert(proto::kOffWeaponTable + proto::kWeaponTableBytes <= SERVER_VIEW_BYTES, "server.dll writes the weapon table");
		static_assert(proto::kOffMobTable + proto::kMobTableBytes <= SERVER_VIEW_BYTES, "server.dll reads the mob table");
		static_assert(proto::kOffHostDebug + proto::kHostDebugBytes <= SERVER_VIEW_BYTES, "server.dll writes its part of the host debug");
		static_assert(SERVER_VIEW_BYTES <= proto::kOffOverlayPixels, "the overlay and the render ring are client.dll's");

		// who may open the mapping: this windows user (plus system and administrators) at normal
		// integrity. said explicitly because a game run as administrator would otherwise make it
		// administrators-only, and minecraft (never elevated) could not open it. free with LocalFree.
		PSECURITY_DESCRIPTOR shared_with_this_user()
		{
			std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";
			HANDLE       token = nullptr;
			if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
				DWORD size = 0;
				::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
				std::vector<std::uint8_t> buffer(size);
				if (size && ::GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
					LPWSTR sid = nullptr;
					if (::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) {
						sddl += std::wstring(L"(A;;GA;;;") + sid + L")";
						::LocalFree(sid);
					}
				}
				::CloseHandle(token);
			}
			sddl += L"S:(ML;;NW;;;ME)";
			PSECURITY_DESCRIPTOR descriptor = nullptr;
			if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
				log_warning("shared memory: couldn't build its access rules (%lu); using the defaults", ::GetLastError());
				return nullptr;
			}
			return descriptor;
		}

		template <class T>
		void seqlock_write(T* dst, const T& src)
		{
			auto&      seq = as_atomic(dst->seq);
			const auto s = seq.load(std::memory_order_relaxed);
			seq.store(s + 1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			std::memcpy(reinterpret_cast<std::uint8_t*>(dst) + 4, reinterpret_cast<const std::uint8_t*>(&src) + 4, sizeof(T) - 4);
			seq.store(s + 2, std::memory_order_release);
		}

		template <class T>
		bool seqlock_read(const T* src, T& out, int attempts)
		{
			const auto& seq = as_atomic(src->seq);
			for (int attempt = 0; attempt < attempts; ++attempt) {
				const auto s1 = seq.load(std::memory_order_acquire);
				if (s1 & 1) {
					_mm_pause();
					continue;
				}
				std::memcpy(&out, src, sizeof(T));
				std::atomic_thread_fence(std::memory_order_acquire);
				if (seq.load(std::memory_order_relaxed) == s1) {
					return true;
				}
			}
			return false;
		}
	}

	Link::~Link()
	{
		if (base_) {
			::UnmapViewOfFile(base_);
		}
		if (mapping_) {
			::CloseHandle(mapping_);
		}
	}

	bool Link::create()
	{
		return map(true, proto::kMappingBytes);
	}

	bool Link::attach()
	{
		return map(false, SERVER_VIEW_BYTES);
	}

	bool Link::map(bool reset, std::uint64_t view_bytes)
	{
		if (base_) {
			return true;
		}
		const auto          size = proto::kMappingBytes;
		SECURITY_ATTRIBUTES access{ sizeof(access), shared_with_this_user(), FALSE };
		mapping_ = ::CreateFileMappingW(INVALID_HANDLE_VALUE, access.lpSecurityDescriptor ? &access : nullptr, PAGE_READWRITE,
			static_cast<DWORD>(size >> 32), static_cast<DWORD>(size & 0xFFFFFFFF), proto::kMappingName);
		const DWORD created = ::GetLastError();
		if (access.lpSecurityDescriptor) {
			::LocalFree(access.lpSecurityDescriptor);
		}
		if (!mapping_) {
			log_error("CreateFileMapping failed (%lu)", created);
			return false;
		}
		const bool existed = created == ERROR_ALREADY_EXISTS;
		// the mapping is always created whole (minecraft maps all of it); the view is what this
		// process pays for in address space
		base_ = static_cast<std::uint8_t*>(::MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(view_bytes)));
		if (!base_) {
			log_error("MapViewOfFile of %llu MB failed (%lu)", static_cast<unsigned long long>(view_bytes >> 20), ::GetLastError());
			::CloseHandle(mapping_);
			mapping_ = nullptr;
			return false;
		}
		view_bytes_ = view_bytes;
		if (!reset) {
			log_info("shared memory attached (%llu of %llu MB, %s)", static_cast<unsigned long long>(view_bytes >> 20),
				static_cast<unsigned long long>(size >> 20), existed ? "existing" : "new");
			return true;
		}

		// a stale mapping survives while minecraft still has it open from an earlier game run: reset
		// what the host owns so the overlay swap and our tables start from a known state. the rings
		// keep their indices: minecraft goes on writing for a while after a game run ends (until the
		// host's heartbeat times out), and zeroing an index it owns between its read and its write sent
		// it back to the old value, leaving this run chewing through stale messages (old clear-alls
		// among them) while minecraft took its resend as delivered: an empty world until it restarted.
		// its rings skip what's pending; ours go on from where the last run stopped.
		auto* header = at<proto::Header>(proto::kOffHeader);
		std::memset(base_ + proto::kOffHostState, 0, sizeof(proto::HostState));
		std::memset(base_ + proto::kOffOverlayCtl, 0, 0x100);
		std::memset(base_ + proto::kOffActorTable, 0, sizeof(proto::ActorTable));
		std::memset(base_ + proto::kOffWorldEntities, 0, sizeof(proto::WorldEntities));
		std::memset(base_ + proto::kOffHostDebug, 0, proto::kHostDebugBytes);
		std::memset(base_ + proto::kOffWeaponTable, 0, proto::kWeaponTableBytes);
		std::memset(base_ + proto::kOffMobTable, 0, proto::kMobTableBytes);
		std::memset(base_ + proto::kOffWaterProbes + proto::kWaterProbesAnswerOff, 0, sizeof(proto::WaterProbes));
		skip_pending(proto::kOffRenderRing + proto::kRenRingHeadOff, proto::kOffRenderRing + proto::kRenRingTailOff);
		skip_pending(proto::kOffEventRing + proto::kEventRingHeadOff, proto::kOffEventRing + proto::kEventRingTailOff);
		header->version = proto::kVersion;
		header->hostPid = ::GetCurrentProcessId();
		header->hostHeartbeatMs = ::GetTickCount64();
		// the performance counter only grows, so no two runs on this machine share a session
		LARGE_INTEGER now{};
		::QueryPerformanceCounter(&now);
		as_atomic(header->hostSession).store(static_cast<std::uint64_t>(now.QuadPart) | 1, std::memory_order_release);
		as_atomic(header->magic).store(proto::kMagic, std::memory_order_release);

		log_info("shared memory created (%llu MB, %s)", static_cast<unsigned long long>(size >> 20), existed ? "reused" : "new");
		return true;
	}

	void Link::skip_pending(std::uint64_t head_offset, std::uint64_t tail_offset)
	{
		const auto head = as_atomic(*at<std::uint64_t>(head_offset)).load(std::memory_order_acquire);
		as_atomic(*at<std::uint64_t>(tail_offset)).store(head, std::memory_order_release);
	}

	bool Link::mc_alive() const
	{
		if (!base_) {
			return false;
		}
		const auto last = as_atomic(at<proto::Header>(proto::kOffHeader)->mcHeartbeatMs).load(std::memory_order_acquire);
		return last != 0 && ::GetTickCount64() - last < MC_TIMEOUT_MS;
	}

	std::uint32_t Link::mc_pid() const
	{
		return base_ ? as_atomic(at<proto::Header>(proto::kOffHeader)->mcPid).load(std::memory_order_acquire) : 0;
	}

	void Link::heartbeat()
	{
		if (base_) {
			as_atomic(at<proto::Header>(proto::kOffHeader)->hostHeartbeatMs).store(::GetTickCount64(), std::memory_order_release);
		}
	}

	void Link::write_host_state(const proto::HostState& state)
	{
		if (base_) {
			seqlock_write(at<proto::HostState>(proto::kOffHostState), state);
		}
	}

	bool Link::read_host_state(proto::HostState& out) const
	{
		return base_ && seqlock_read(at<proto::HostState>(proto::kOffHostState), out, 64);
	}

	bool Link::read_mc_state(proto::McState& out) const
	{
		return base_ && seqlock_read(at<proto::McState>(proto::kOffMcState), out, 64);
	}

	void Link::write_water_grid(const proto::WaterGrid& grid)
	{
		if (base_) {
			seqlock_write(at<proto::WaterGrid>(proto::kOffWaterGrid), grid);
		}
	}

	bool Link::read_water_probe_requests(proto::WaterProbeRequests& out) const
	{
		if (!base_ || !seqlock_read(at<proto::WaterProbeRequests>(proto::kOffWaterProbes), out, 16)) {
			return false;
		}
		out.count = std::min(out.count, proto::kMaxWaterProbes);
		return true;
	}

	void Link::write_water_probes(const proto::WaterProbes& probes)
	{
		if (base_) {
			seqlock_write(at<proto::WaterProbes>(proto::kOffWaterProbes + proto::kWaterProbesAnswerOff), probes);
		}
	}

	void Link::write_host_debug(const proto::HostDebug& debug)
	{
		if (base_) {
			seqlock_write(at<proto::HostDebug>(proto::kOffHostDebug), debug);
		}
	}

	void Link::write_host_debug_server(const proto::HostDebugServer& debug)
	{
		if (base_) {
			seqlock_write(at<proto::HostDebugServer>(proto::kOffHostDebug + proto::kHostDebugServerOff), debug);
		}
	}

	Link::Backlog Link::backlog() const
	{
		Backlog backlog;
		if (!base_) {
			return backlog;
		}
		const auto behind = [this](std::uint64_t ring, std::uint64_t head_offset, std::uint64_t tail_offset) -> std::uint64_t {
			const auto head = as_atomic(*at<std::uint64_t>(ring + head_offset)).load(std::memory_order_acquire);
			const auto tail = as_atomic(*at<std::uint64_t>(ring + tail_offset)).load(std::memory_order_acquire);
			return head > tail ? head - tail : 0;
		};
		backlog.input = behind(proto::kOffInputRing, proto::kInputRingHeadOff, proto::kInputRingTailOff);
		backlog.events = behind(proto::kOffEventRing, proto::kEventRingHeadOff, proto::kEventRingTailOff);
		backlog.collision_bytes = behind(proto::kOffCollisionRing, proto::kColRingHeadOff, proto::kColRingTailOff);
		if (maps(proto::kOffRenderRing + proto::kRenRingDataOff)) {
			backlog.render_bytes = behind(proto::kOffRenderRing, proto::kRenRingHeadOff, proto::kRenRingTailOff);
		}
		return backlog;
	}

	void Link::push_input(proto::InputType type, std::uint16_t code, std::int32_t a, std::int32_t b, std::int32_t c)
	{
		if (!base_) {
			return;
		}
		auto*      ring = base_ + proto::kOffInputRing;
		auto&      head_ref = *reinterpret_cast<std::uint64_t*>(ring + proto::kInputRingHeadOff);
		auto&      tail_ref = *reinterpret_cast<std::uint64_t*>(ring + proto::kInputRingTailOff);
		const auto head = as_atomic(head_ref).load(std::memory_order_relaxed);
		const auto tail = as_atomic(tail_ref).load(std::memory_order_acquire);
		if (head - tail >= proto::kInputRingEntries) {
			return;
		}
		auto* entry = reinterpret_cast<proto::InputEvent*>(ring + proto::kInputRingDataOff) + (head & (proto::kInputRingEntries - 1));
		*entry = { static_cast<std::uint16_t>(type), code, a, b, c };
		as_atomic(head_ref).store(head + 1, std::memory_order_release);
	}

	bool Link::push_string(proto::StringChannel channel, std::string_view utf8)
	{
		if (!base_) {
			return false;
		}
		const std::size_t pieces = std::max<std::size_t>(1, (utf8.size() + proto::kStringPieceBytes - 1) / proto::kStringPieceBytes);
		// all or nothing: a string missing a piece would reach minecraft garbled
		auto*      ring = base_ + proto::kOffInputRing;
		const auto head = as_atomic(*reinterpret_cast<std::uint64_t*>(ring + proto::kInputRingHeadOff)).load(std::memory_order_relaxed);
		const auto tail = as_atomic(*reinterpret_cast<std::uint64_t*>(ring + proto::kInputRingTailOff)).load(std::memory_order_acquire);
		if (proto::kInputRingEntries - (head - tail) < pieces) {
			return false;
		}
		for (std::size_t piece = 0; piece < pieces; ++piece) {
			const std::size_t offset = piece * proto::kStringPieceBytes;
			const std::size_t bytes = std::min<std::size_t>(proto::kStringPieceBytes, utf8.size() - offset);
			std::int32_t      words[3]{};
			std::memcpy(words, utf8.data() + offset, bytes);
			const auto code = static_cast<std::uint16_t>(channel | (bytes << proto::kStringBytesShift) | (piece + 1 == pieces ? proto::kStringEnd : 0));
			push_input(proto::kInString, code, words[0], words[1], words[2]);
		}
		return true;
	}

	bool Link::write_collision(proto::ColType type, const void* payload, std::uint32_t bytes)
	{
		if (!base_) {
			return false;
		}
		auto*          ring = base_ + proto::kOffCollisionRing;
		auto&          head_ref = *reinterpret_cast<std::uint64_t*>(ring + proto::kColRingHeadOff);
		auto&          tail_ref = *reinterpret_cast<std::uint64_t*>(ring + proto::kColRingTailOff);
		auto*          data = ring + proto::kColRingDataOff;
		constexpr auto size = proto::kColRingDataBytes;

		const std::uint64_t msg_bytes = (sizeof(proto::ColMsgHeader) + bytes + 7) & ~7ull;
		if (msg_bytes > size / 2) {
			log_error("collision message too large (%llu bytes)", static_cast<unsigned long long>(msg_bytes));
			return false;
		}
		auto       head = as_atomic(head_ref).load(std::memory_order_relaxed);
		const auto tail = as_atomic(tail_ref).load(std::memory_order_acquire);
		auto       pos = head % size;
		const auto pad_bytes = (pos + msg_bytes > size) ? size - pos : 0;
		if (size - (head - tail) < msg_bytes + pad_bytes) {
			return false;
		}
		if (pad_bytes) {
			*reinterpret_cast<proto::ColMsgHeader*>(data + pos) = { proto::kColPad, 0 };
			head += pad_bytes;
			pos = 0;
		}
		*reinterpret_cast<proto::ColMsgHeader*>(data + pos) = { type, bytes };
		std::memcpy(data + pos + sizeof(proto::ColMsgHeader), payload, bytes);
		as_atomic(head_ref).store(head + msg_bytes, std::memory_order_release);
		return true;
	}

	void Link::write_actors(const proto::ActorRecord* records, std::uint32_t count)
	{
		if (!base_) {
			return;
		}
		auto*      table = at<proto::ActorTable>(proto::kOffActorTable);
		auto&      seq = as_atomic(table->seq);
		const auto s = seq.load(std::memory_order_relaxed);
		seq.store(s + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		const auto n = std::min(count, proto::kMaxActors);
		table->count = n;
		if (n) {
			std::memcpy(table->actors, records, sizeof(proto::ActorRecord) * n);
		}
		seq.store(s + 2, std::memory_order_release);
	}

	void Link::write_weapons(const proto::WeaponTable& table)
	{
		if (base_) {
			seqlock_write(at<proto::WeaponTable>(proto::kOffWeaponTable), table);
		}
	}

	bool Link::read_weapons(proto::WeaponTable& out) const
	{
		return base_ && seqlock_read(at<proto::WeaponTable>(proto::kOffWeaponTable), out, 64);
	}

	bool Link::pop_event(proto::McEvent& out)
	{
		if (!base_) {
			return false;
		}
		auto*      ring = base_ + proto::kOffEventRing;
		auto&      head_ref = *reinterpret_cast<std::uint64_t*>(ring + proto::kEventRingHeadOff);
		auto&      tail_ref = *reinterpret_cast<std::uint64_t*>(ring + proto::kEventRingTailOff);
		const auto head = as_atomic(head_ref).load(std::memory_order_acquire);
		auto       tail = as_atomic(tail_ref).load(std::memory_order_relaxed);
		if (tail >= head) {
			return false;
		}
		if (head - tail > proto::kEventRingEntries) {
			tail = head - proto::kEventRingEntries;
		}
		out = reinterpret_cast<const proto::McEvent*>(ring + proto::kEventRingDataOff)[tail & (proto::kEventRingEntries - 1)];
		as_atomic(tail_ref).store(tail + 1, std::memory_order_release);
		return true;
	}

	bool Link::read_world_entities(proto::WorldEntities& out) const
	{
		if (!base_) {
			return false;
		}
		const auto* src = at<proto::WorldEntities>(proto::kOffWorldEntities);
		const auto& seq = as_atomic(src->seq);
		for (int attempt = 0; attempt < 16; ++attempt) {
			const auto s1 = seq.load(std::memory_order_acquire);
			if (s1 & 1) {
				_mm_pause();
				continue;
			}
			const auto count = std::min(src->count, proto::kMaxWorldEntities);
			std::memcpy(&out, src, offsetof(proto::WorldEntities, entities) + sizeof(proto::WorldEntity) * count);
			out.count = count;
			std::atomic_thread_fence(std::memory_order_acquire);
			if (seq.load(std::memory_order_relaxed) == s1) {
				return true;
			}
		}
		return false;
	}

	bool Link::read_mobs(proto::MobTable& out) const
	{
		if (!base_) {
			return false;
		}
		const auto* src = at<proto::MobTable>(proto::kOffMobTable);
		const auto& seq = as_atomic(src->seq);
		for (int attempt = 0; attempt < 16; ++attempt) {
			const auto s1 = seq.load(std::memory_order_acquire);
			if (s1 & 1) {
				_mm_pause();
				continue;
			}
			const auto count = std::min(src->count, proto::kMaxMobs);
			std::memcpy(&out, src, offsetof(proto::MobTable, mobs) + sizeof(proto::MobRecord) * count);
			out.count = count;
			std::atomic_thread_fence(std::memory_order_acquire);
			if (seq.load(std::memory_order_relaxed) == s1) {
				return true;
			}
		}
		return false;
	}

	void Link::drain_render(const std::function<void(std::uint32_t, const std::uint8_t*, std::uint32_t)>& fn, std::uint64_t max_bytes)
	{
		// past an attach()ed view's end: the render ring is client.dll's
		if (!maps(proto::kOffRenderRing + proto::kRenderRingBytes)) {
			return;
		}
		auto*          ring = base_ + proto::kOffRenderRing;
		auto&          head_ref = *reinterpret_cast<std::uint64_t*>(ring + proto::kRenRingHeadOff);
		auto&          tail_ref = *reinterpret_cast<std::uint64_t*>(ring + proto::kRenRingTailOff);
		const auto     head = as_atomic(head_ref).load(std::memory_order_acquire);
		auto           tail = as_atomic(tail_ref).load(std::memory_order_relaxed);
		auto*          data = ring + proto::kRenRingDataOff;
		constexpr auto size = proto::kRenRingDataBytes;
		std::uint64_t  done = 0;
		while (tail < head && done < max_bytes) {
			const auto  pos = tail % size;
			const auto* hdr = reinterpret_cast<const proto::ColMsgHeader*>(data + pos);
			if (hdr->type == proto::kRenPad) {
				tail += size - pos;
				continue;
			}
			if (size - pos < sizeof(proto::ColMsgHeader) || hdr->payloadBytes > size - pos - sizeof(proto::ColMsgHeader)) {
				// not a message minecraft wrote (the ring lost its place): drop what's pending, it resends
				log_error("render ring out of step at %llu (message of %u bytes): skipping to %llu", static_cast<unsigned long long>(tail), hdr->payloadBytes,
					static_cast<unsigned long long>(head));
				tail = head;
				break;
			}
			fn(hdr->type, data + pos + sizeof(proto::ColMsgHeader), hdr->payloadBytes);
			const auto msg_bytes = (sizeof(proto::ColMsgHeader) + hdr->payloadBytes + 7) & ~7ull;
			tail += msg_bytes;
			done += msg_bytes;
		}
		as_atomic(tail_ref).store(tail, std::memory_order_release);
	}

	bool Link::acquire_overlay_frame()
	{
		// the slots' pixels lie past an attach()ed view's end, and front_pixels() is read after this
		if (!maps(proto::kOffOverlayPixels + proto::kOverlaySlotBytes * proto::kOverlaySlots)) {
			return false;
		}
		auto& state = as_atomic(at<proto::OverlayCtl>(proto::kOffOverlayCtl)->state);
		if (!(state.load(std::memory_order_acquire) & proto::kOverlayDirty)) {
			return false;
		}
		const auto old = state.exchange(overlay_front_, std::memory_order_acq_rel);
		overlay_front_ = old & 3;
		return true;
	}

	void Link::reset_overlay()
	{
		if (!base_) {
			return;
		}
		as_atomic(at<proto::OverlayCtl>(proto::kOffOverlayCtl)->state).store(0, std::memory_order_release);
		overlay_front_ = 2;
	}

	const std::uint8_t* Link::front_pixels() const
	{
		return base_ + proto::kOffOverlayPixels + proto::kOverlaySlotBytes * overlay_front_;
	}

	const proto::OverlaySlotHdr* Link::front_header() const
	{
		return at<proto::OverlaySlotHdr>(proto::kOffOverlaySlotHdr + sizeof(proto::OverlaySlotHdr) * overlay_front_);
	}
}
