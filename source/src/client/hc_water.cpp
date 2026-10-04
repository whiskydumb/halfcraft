// client.dll: half-life's water around minecraft's player (proto::WaterGrid), so minecraft swims,
// floats and drowns in half-life's canals and pools. minecraft treats the air below the surface
// over each block column as water; the surface is found with point contents probes, top down,
// in a window around the player's feet.

#include "cbase.h"
#include "engine/IEngineTrace.h"

#include "tier0/valve_minmax_off.h"
#include <cmath>

#include "client/hc_client.h"
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
		constexpr double BELOW = 6.0;   // and below them they stop
		constexpr double STEP = 0.5;    // blocks between probes
		constexpr double CLIMB = 32.0;  // how far up a column under water at the window's top is followed
		constexpr int    REFINES = 6;   // halvings of a step: the surface to 1/128 block
		constexpr float  REFRESH_SECONDS = 0.5f;  // moving water (lowering canals) shows up this fast

		bool wet(double x, double y, double z, int slot)
		{
			float p[3];
			mc_to_source(x, y, z, slot, p);
			return (enginetrace->GetPointContents(Vector(p[0], p[1], p[2])) & WET) != 0;
		}

		/// the surface between a wet height and a dry one above it.
		double refine(double wet_y, double dry_y, double x, double z, int slot)
		{
			for (int i = 0; i < REFINES; ++i) {
				const double mid = (wet_y + dry_y) * 0.5;
				(wet(x, mid, z, slot) ? wet_y : dry_y) = mid;
			}
			return wet_y;
		}

		/// minecraft y of the water surface over a column near the feet, or kNoWater.
		float surface(double x, double z, double feet, int slot)
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
			proto::WaterGrid grid_{};
			float            refresh_in_ = 0.0f;
		};

		HalfCraftWaterSystem g_water_system;
	}
}
