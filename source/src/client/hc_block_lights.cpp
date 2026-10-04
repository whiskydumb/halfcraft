// client.dll: minecraft's light-emitting blocks as source dynamic lights (see hc_block_lights.h).

#include "cbase.h"
#include "dlight.h"
#include "iefx.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "client/hc_block_lights.h"
#include "client/hc_client.h"
#include "core/hc_units.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr int   CELL_BLOCKS = 3;          // nearby emitters merge into one light per cell (a lava lake would be hundreds)
		constexpr int   MAX_WORLD_LIGHTS = 12;    // of source's 32 dlights, shared with muzzle flashes and the like
		constexpr int   MAX_LIGHTS = 28;          // world lights + character-only lights
		constexpr float RANGE_BLOCKS = 48.0f;     // emitters further than this from the player stay dark
		constexpr float MAX_RADIUS_BLOCKS = 12.0f;
		constexpr float RADIUS_PER_LEVEL = 0.6f;  // blocks of reach per minecraft light level
		constexpr float REBUILD_SECONDS = 0.25f;
		constexpr float KEEP_ALIVE_SECONDS = 0.2f;   // source drops a light we stop refreshing after this
		constexpr int   LIGHT_KEY = 0x48430000;      // dlight keys: past any entity index
		constexpr float LIGHT_INTENSITY = 0.6f;      // linear light at the centre of a level 15 light, before hc_torch_light
		// source lights its world (lightmaps) far more faintly than its models for the same light: measured
		// in game, a wall 1-2 blocks away barely shows exponent 0-1 and clearly shows 2-3
		constexpr float WORLD_GAIN = 8.0f;

		ConVar hc_torch_light("hc_torch_light", "1", FCVAR_ARCHIVE, "halfcraft: brightness of minecraft's torches, lava and glowstone on half-life's world (0 = off)");
		ConVar hc_torch_lights("hc_torch_lights", "8", FCVAR_ARCHIVE,
			"halfcraft: how many of the nearest minecraft lights also light half-life's world, not only its characters (up to 12; they cost the most)");

		struct LightSource
		{
			std::int32_t  x, y, z;  // minecraft block
			std::uint8_t  level;    // 1-15
			std::uint8_t  kind;     // proto::LightKind
			std::uint8_t  hazard;   // proto::BlockHazard
			std::uint32_t rgb;      // red in the low byte
		};

		struct Cluster
		{
			std::uint64_t cell = 0;
			double        x = 0, y = 0, z = 0, weight = 0;  // weighted minecraft position
			float         r = 0, g = 0, b = 0;
			int           level = 0, count = 0;
			std::uint8_t  kind = proto::kLightSteady;
			float         radius = 0;  // units
			float         score = 0;   // nearest first
			Vector        origin;      // source
		};

		std::uint64_t pack(std::int32_t x, std::int32_t y, std::int32_t z)
		{
			return (std::uint64_t(std::uint32_t(x) & 0x3FFFFF) << 42) | (std::uint64_t(std::uint32_t(z) & 0x3FFFFF) << 20) | std::uint64_t(std::uint32_t(y) & 0xFFFFF);
		}

		int floor_div(std::int32_t v, int d)
		{
			return v >= 0 ? v / d : (v - d + 1) / d;
		}

		class BlockLights
		{
		public:
			void on_lights(const std::uint8_t* payload, std::uint32_t bytes)
			{
				if (bytes < sizeof(proto::RenLights)) {
					return;
				}
				proto::RenLights header;
				std::memcpy(&header, payload, sizeof(header));
				const auto key = pack(header.sx, header.sy, header.sz);
				const auto count = std::min<std::uint64_t>(header.count, (bytes - sizeof(header)) / sizeof(proto::RenLight));
				std::lock_guard<std::mutex> lock(hazards_lock_);
				if (const auto it = by_section_.find(key); it != by_section_.end()) {
					for (const auto& e : it->second) {
						if (e.hazard) {
							hazards_.erase(pack(e.x, e.y, e.z));
						}
					}
				}
				rebuild_in_ = 0.0f;  // show a new torch at once
				if (count == 0) {
					by_section_.erase(key);
					return;
				}
				auto& list = by_section_[key];
				list.clear();
				const auto* lights = reinterpret_cast<const proto::RenLight*>(payload + sizeof(header));
				for (std::uint64_t i = 0; i < count; ++i) {
					const auto&        l = lights[i];
					const std::uint8_t top = static_cast<std::uint8_t>(l.color >> 24);
					const LightSource  e{ header.sx * 16 + l.x, header.sy * 16 + l.y, header.sz * 16 + l.z, l.level, static_cast<std::uint8_t>(top & 0x0F),
						 static_cast<std::uint8_t>(top >> 4), l.color & 0xFFFFFF };
					list.push_back(e);
					if (e.hazard) {
						hazards_[pack(e.x, e.y, e.z)] = e.hazard;
					}
				}
			}

			void clear()
			{
				std::lock_guard<std::mutex> lock(hazards_lock_);
				by_section_.clear();
				hazards_.clear();
				chosen_.clear();
				rebuild_in_ = 0.0f;
			}

			/// proto::BlockHazard of a minecraft block (fire, lava, magma), from any thread.
			int hazard_at(std::int32_t x, std::int32_t y, std::int32_t z)
			{
				std::lock_guard<std::mutex> lock(hazards_lock_);
				const auto it = hazards_.find(pack(x, y, z));
				return it != hazards_.end() ? it->second : proto::kHazardNone;
			}

			void update(float frametime);

		private:
			void rebuild(const McVec& player, int slot);
			void refresh(float now);

			std::unordered_map<std::uint64_t, std::vector<LightSource>> by_section_;
			std::mutex                                                   hazards_lock_;
			std::unordered_map<std::uint64_t, std::uint8_t>             hazards_;  // block -> proto::BlockHazard
			std::vector<Cluster>                                         chosen_;  // nearest first
			float                                                        rebuild_in_ = 0.0f;
			float                                                        clock_ = 0.0f;
		};

		void BlockLights::rebuild(const McVec& player, int slot)
		{
			std::unordered_map<std::uint64_t, Cluster> cells;
			const double                               range2 = double(RANGE_BLOCKS) * RANGE_BLOCKS;
			for (const auto& entry : by_section_) {
				for (const auto& e : entry.second) {
					const double dx = e.x + 0.5 - player.x, dy = e.y + 0.5 - player.y, dz = e.z + 0.5 - player.z;
					if (dx * dx + dy * dy + dz * dz > range2) {
						continue;
					}
					const auto cell = pack(floor_div(e.x, CELL_BLOCKS), floor_div(e.y, CELL_BLOCKS), floor_div(e.z, CELL_BLOCKS));
					auto&      c = cells[cell];
					c.cell = cell;
					const double w = double(e.level) * e.level;
					c.x += (e.x + 0.5) * w;
					c.y += (e.y + 0.5) * w;
					c.z += (e.z + 0.5) * w;
					c.weight += w;
					c.r += float(e.rgb & 0xFF) / 255.0f * float(w);
					c.g += float((e.rgb >> 8) & 0xFF) / 255.0f * float(w);
					c.b += float((e.rgb >> 16) & 0xFF) / 255.0f * float(w);
					c.level = std::max<int>(c.level, e.level);
					c.kind = std::max(c.kind, e.kind);
					++c.count;
				}
			}
			float player_source[3];
			mc_to_source(player.x, player.y, player.z, slot, player_source);
			const Vector eye(player_source[0], player_source[1], player_source[2]);
			chosen_.clear();
			for (auto& entry : cells) {
				Cluster& c = entry.second;
				const float w = float(c.weight);
				c.r /= w, c.g /= w, c.b /= w;
				// a light level reaches about that many blocks in minecraft; a cluster of many a bit further
				const float blocks = float(c.level + 1) * RADIUS_PER_LEVEL * (1.0f + 0.12f * std::log2(float(c.count)));
				c.radius = std::min(blocks, MAX_RADIUS_BLOCKS) * static_cast<float>(UNITS_PER_BLOCK);
				// emitters sit a little above the blocks' centre (lava lights its surface)
				float origin[3];
				mc_to_source(c.x / c.weight, c.y / c.weight + 0.3, c.z / c.weight, slot, origin);
				c.origin.Init(origin[0], origin[1], origin[2]);
				c.score = c.origin.DistTo(eye) - c.radius;
				chosen_.push_back(c);
			}
			const auto keep = std::min<std::size_t>(chosen_.size(), MAX_LIGHTS);
			std::partial_sort(chosen_.begin(), chosen_.begin() + keep, chosen_.end(), [](const Cluster& a, const Cluster& b) { return a.score < b.score; });
			chosen_.resize(keep);
		}

		void BlockLights::refresh(float now)
		{
			const float brightness = std::max(0.0f, hc_torch_light.GetFloat());
			const int   world_lights = std::clamp(hc_torch_lights.GetInt(), 0, MAX_WORLD_LIGHTS);
			for (std::size_t i = 0; i < chosen_.size(); ++i) {
				const Cluster& c = chosen_[i];
				// flames flicker, lava glows slowly, the rest stay put
				const float t = clock_ + float(c.cell % 997) * 0.37f;
				float       k = 1.0f;
				if (c.kind == proto::kLightFlame) {
					k = 1.0f + 0.07f * std::sin(t * 9.1f) + 0.05f * std::sin(t * 23.7f + 1.3f) + 0.03f * std::sin(t * 4.3f + 0.7f);
				} else if (c.kind == proto::kLightLava) {
					k = 1.0f + 0.08f * std::sin(t * 1.3f) + 0.03f * std::sin(t * 3.1f + 2.0f);
				}
				// brighter emitters shine harder as well as further
				const float intensity = LIGHT_INTENSITY * brightness * (0.35f + 0.65f * float(c.level) / 15.0f) * k;
				auto        place = [&](dlight_t* light, float scale, int flags) {
                    if (!light) {
                        return;
                    }
                    light->origin = c.origin;
                    light->radius = c.radius;
                    VectorToColorRGBExp32(Vector(c.r, c.g, c.b) * (intensity * scale), light->color);
                    light->die = now + KEEP_ALIVE_SECONDS;
                    light->decay = 0.0f;
                    light->minlight = 0.0f;
                    light->style = 0;
                    light->flags = flags;
				};
				// the nearest light the map's lightmaps; every one lights characters
				const int key = LIGHT_KEY + static_cast<int>(i);
				if (static_cast<int>(i) < world_lights) {
					place(effects->CL_AllocDlight(key), WORLD_GAIN, DLIGHT_NO_MODEL_ILLUMINATION);
				}
				place(effects->CL_AllocElight(key), 1.0f, 0);
			}
		}

		void BlockLights::update(float frametime)
		{
			clock_ += frametime;
			auto&      s = client_session();
			const bool active = s.have_mc && s.mc_in_world && !s.loading && hc_torch_light.GetFloat() > 0.0f && engine->IsInGame();
			if (!active) {
				chosen_.clear();  // the lights die by themselves without refreshes
				rebuild_in_ = 0.0f;
				return;
			}
			rebuild_in_ -= frametime;
			if (rebuild_in_ <= 0.0f) {
				rebuild_in_ = REBUILD_SECONDS;
				rebuild({ s.mc.x, s.mc.y, s.mc.z }, s.slot);
			}
			refresh(gpGlobals->curtime);
		}

		BlockLights& block_lights()
		{
			static BlockLights instance;
			return instance;
		}

		class HalfCraftBlockLightsSystem final : public CAutoGameSystemPerFrame
		{
		public:
			HalfCraftBlockLightsSystem() : CAutoGameSystemPerFrame("HalfCraftBlockLights") {}

			void Update(float frametime) override { block_lights().update(frametime); }
		};

		HalfCraftBlockLightsSystem g_block_lights_system;
	}

	void block_lights_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes)
	{
		if (type == proto::kRenLights) {
			block_lights().on_lights(payload, bytes);
		} else if (type == proto::kRenClearAll) {
			block_lights().clear();
		}
	}
}

// server.dll's way to minecraft's fire, lava and magma (hc_bridge.h)
extern "C" __declspec(dllexport) int HalfCraft_HazardAt(int x, int y, int z)
{
	return halfcraft::block_lights().hazard_at(x, y, z);
}
