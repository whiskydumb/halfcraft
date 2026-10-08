// client.dll: half-life's water around minecraft's player (proto::WaterGrid), so minecraft swims,
// floats and drowns in half-life's canals and pools. minecraft treats the air below the surface
// over each block column as water; the surface is found with point contents probes, top down,
// in a window around the player's feet. a few boats and fishing bobbers, and mobs and dropped items off
// that window, get a smaller grid of their own the same way (proto::WaterProbes), which reaches them
// away from the player or far below.

#include "cbase.h"
#include "engine/IEngineTrace.h"

#include "tier0/valve_minmax_off.h"
#include <climits>
#include <cmath>
#include <cstdint>

#include "client/hc_client.h"
#include "core/hc_log.h"
#include "core/hc_units.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr int    GRID = static_cast<int>(proto::kWaterGridSize);
		constexpr int    WET = CONTENTS_WATER | CONTENTS_SLIME;
		constexpr double ABOVE = 3.0;   // blocks above the feet the probes start
		constexpr double BELOW = proto::kWaterProbeDepth;  // and below them they stop
		constexpr double STEP = 0.5;    // blocks between probes
		constexpr double CLIMB = 32.0;  // how far up a column under water at the window's top is followed
		constexpr int    REFINES = 6;   // halvings of a step: the surface to 1/128 block
		constexpr float  REFRESH_SECONDS = 0.5f;  // moving water (lowering canals) shows up this fast
		constexpr int    PROBE = static_cast<int>(proto::kWaterProbeSize);
		constexpr double PROBE_MOVED = 1.5;  // blocks a probed thing goes up or down before its grid is probed again
		constexpr int    PROBE_KEEP = 2;     // columns of its grid a probed thing keeps on every side before the grid follows it

		bool wet(double x, double y, double z, MapSlot slot)
		{
			float p[3];
			mc_to_source(x, y, z, slot, p);
			return (enginetrace->GetPointContents(Vector(p[0], p[1], p[2])) & WET) != 0;
		}

		/// the surface between a wet height and a dry one above it.
		double refine(double wet_y, double dry_y, double x, double z, MapSlot slot)
		{
			for (int i = 0; i < REFINES; ++i) {
				const double mid = (wet_y + dry_y) * 0.5;
				(wet(x, mid, z, slot) ? wet_y : dry_y) = mid;
			}
			return wet_y;
		}

		/// minecraft y of the water surface over a column near the feet, or kNoWater.
		float surface(double x, double z, double feet, MapSlot slot)
		{
			const double top = feet + ABOVE;
			if (wet(x, top, z, slot)) {
				// deep under water: follow the column up to its surface
				double y = top;
				while (y < top + CLIMB && wet(x, y + 1.0, z, slot)) {
					y += 1.0;
				}
				return static_cast<float>(refine(y, y + 1.0, x, z, slot));
			}
			for (double y = top - STEP; y >= feet - BELOW; y -= STEP) {
				if (wet(x, y, z, slot)) {
					return static_cast<float>(refine(y, y + STEP, x, z, slot));
				}
			}
			return proto::kNoWater;
		}

		class HalfCraftWaterSystem final : public CAutoGameSystemPerFrame
		{
		public:
			HalfCraftWaterSystem() : CAutoGameSystemPerFrame("HalfCraftWater") {}

			void Update(float frametime) override
			{
				auto& s = client_session();
				if (!s.link_ready || !s.have_mc || !s.mc_in_world || s.loading || !engine->IsInGame()) {
					refresh_in_ = 0.0f;
					return;
				}
				update_probes(s, frametime);
				const int  origin_x = static_cast<int>(std::floor(s.mc.x)) - GRID / 2;
				const int  origin_z = static_cast<int>(std::floor(s.mc.z)) - GRID / 2;
				const bool moved = origin_x != grid_.originX || origin_z != grid_.originZ || s.world_id != grid_.worldId;
				refresh_in_ -= frametime;
				if (!moved && refresh_in_ > 0.0f) {
					return;
				}
				refresh_in_ = REFRESH_SECONDS;
				grid_.originX = origin_x;
				grid_.originZ = origin_z;
				grid_.worldId = s.world_id;
				for (int dz = 0; dz < GRID; ++dz) {
					for (int dx = 0; dx < GRID; ++dx) {
						grid_.surface[dz * GRID + dx] = surface(origin_x + dx + 0.5, origin_z + dz + 0.5, s.mc.y, s.slot);
					}
				}
				s.link.write_water_grid(grid_);
			}

		private:
			/// minecraft's water probes: at most one grid probed a frame, the first whose thing neared its
			/// edge, else the one probed longest ago (once it's REFRESH_SECONDS old).
			void update_probes(ClientSession& s, float frametime)
			{
				proto::WaterProbeRequests requests{};
				if (!s.link.read_water_probe_requests(requests)) {
					return;
				}
				if (s.world_id != probes_world_) {
					// another map: what the grids say is about the last one
					probes_world_ = s.world_id;
					for (auto& grid : probes_.probes) {
						forget(grid);
					}
				}
				if (requests.count != probes_.count) {
					log_info("water probes: minecraft wants the water around %u boat%s or bobber%s", requests.count, requests.count == 1 ? "" : "s",
						requests.count == 1 ? "" : "s");
				}
				int   pick = -1;
				float oldest = REFRESH_SECONDS;
				for (std::uint32_t i = 0; i < requests.count; ++i) {
					ages_[i] += frametime;
				}
				for (std::uint32_t i = 0; i < requests.count; ++i) {
					if (moved_off(requests.at[i], probes_.probes[i])) {
						pick = static_cast<int>(i);
						break;
					}
					if (ages_[i] >= oldest) {
						pick = static_cast<int>(i);
						oldest = ages_[i];
					}
				}
				const bool count_changed = requests.count != probes_.count;
				probes_.count = requests.count;
				if (pick >= 0) {
					probe(requests.at[pick], probes_.probes[pick], s.slot);
					ages_[pick] = 0.0f;
				}
				if (pick >= 0 || count_changed) {
					s.link.write_water_probes(probes_);
				}
			}

			/// the thing at `at` (minecraft coords) is near the edge of the grid probed for it (or off it), or well
			/// above or below it. the margin keeps a thing on a column border, where minecraft's client and
			/// server ticks put it on either side, from moving its grid every frame.
			static bool moved_off(const float (&at)[3], const proto::WaterProbe& grid)
			{
				const std::int64_t dx = static_cast<std::int64_t>(std::floor(at[0])) - grid.originX;
				const std::int64_t dz = static_cast<std::int64_t>(std::floor(at[2])) - grid.originZ;
				return dx < PROBE_KEEP || dx >= PROBE - PROBE_KEEP || dz < PROBE_KEEP || dz >= PROBE - PROBE_KEEP || std::fabs(at[1] - grid.y) > PROBE_MOVED;
			}

			/// the grid of columns around a thing at `at` (minecraft coords).
			static void probe(const float (&at)[3], proto::WaterProbe& out, MapSlot slot)
			{
				out.originX = static_cast<int>(std::floor(at[0])) - PROBE / 2;
				out.originZ = static_cast<int>(std::floor(at[2])) - PROBE / 2;
				out.y = at[1];
				for (int dz = 0; dz < PROBE; ++dz) {
					for (int dx = 0; dx < PROBE; ++dx) {
						out.surface[dz * PROBE + dx] = surface(out.originX + dx + 0.5, out.originZ + dz + 0.5, at[1], slot);
					}
				}
			}

			/// a slot no thing is in yet: no water anywhere, and any request moved off it.
			static void forget(proto::WaterProbe& grid)
			{
				grid.originX = grid.originZ = INT_MIN;
				for (float& height : grid.surface) {
					height = proto::kNoWater;
				}
			}

			proto::WaterGrid   grid_{};
			float              refresh_in_ = 0.0f;
			proto::WaterProbes probes_{};
			std::uint32_t      probes_world_ = 0;
			float              ages_[proto::kMaxWaterProbes] = {};
		};

		HalfCraftWaterSystem g_water_system;
	}
}
