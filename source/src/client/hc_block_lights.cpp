// client.dll: minecraft's light-emitting blocks as source lights (see hc_block_lights.h).

#include "cbase.h"
#include "c_baseplayer.h"
#include "dlight.h"
#include "iclientshadowmgr.h"
#include "iefx.h"
#include "KeyValues.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/itexture.h"
#include "texture_group_names.h"
#include "view_shared.h"
#include "vtf/vtf.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "client/hc_block_lights.h"
#include "client/hc_client.h"
#include "core/hc_units.h"
#include "shared/hc_bridge.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr int   CELL_BLOCKS = 3;          // nearby emitters merge into one light per cell (a lava lake would be hundreds)
		constexpr int   MAX_POINT_LIGHTS = SHADOWED_LIGHTS;  // six projected textures each, with shadow maps (hc_hooks.h)
		constexpr int   MAX_LIGHTS = 24;          // point lights + character-only lights (elights) for the rest
		constexpr float RANGE_BLOCKS = 48.0f;     // emitters further than this from the player stay dark
		constexpr float MAX_RADIUS_BLOCKS = 14.0f;
		constexpr float RADIUS_PER_LEVEL = 0.8f;  // blocks of reach per minecraft light level
		constexpr float CORE_FRACTION = 0.2f;     // full brightness out to this part of the reach, then 1/distance
		constexpr float REBUILD_SECONDS = 0.25f;
		constexpr float KEEP_ALIVE_SECONDS = 0.2f;  // source drops an elight we stop refreshing after this
		constexpr int   LIGHT_KEY = 0x48430000;     // elight keys: past any entity index
		constexpr float POINT_INTENSITY = 1.3f;     // a level 15 point light's core, before hc_torch_light (source's flashlight is 1)
		constexpr float COLOUR_DEPTH = 2.2f;        // minecraft's light colours raised to this: flame-coloured light, not a washed-out white
		constexpr float ELIGHT_INTENSITY = 0.6f;    // the same for characters lit by the lights further away

		// a point light is six projected textures (source's flashlight) looking down the cube's axes. each
		// sees a little past 90 degrees, and its cookie fades out across the seam so that two neighbours
		// add up to one. they cast shadows: besides walls stopping the light, the shadow test is what
		// keeps a face off what's behind it (source's shaders don't clip, so a surface reaching into a
		// face's frustum would also be lit mirrored behind the light)
		constexpr float FACE_HALF_FOV = 48.0f;               // degrees; tan(48) > SEAM_WIDTH
		constexpr float SEAM_WIDTH = 1.1f;                   // the fade runs from tan 1/1.1 to 1.1 off the face's axis
		constexpr int   COOKIE_SIZE = 128;
		// what's behind a face lands outside its depth range, which the shadow test clamps to the far
		// end: a cap just inside it (client_shadow_depth_view) puts it in shadow even where the face sees
		// nothing (sky). the cap has to stay nearer than the far end after the depth bias, hence a near
		// plane well out from the light and a small bias
		constexpr float SHADOW_CAP_FRACTION = 0.95f;
		constexpr float FACE_NEAR = 8.0f;           // units
		constexpr float SHADOW_DEPTH_BIAS = 0.0002f;
		constexpr float SHADOW_SLOPE_BIAS = 16.0f;  // source's flashlight's

		ConVar hc_torch_light("hc_torch_light", "1", FCVAR_ARCHIVE, "halfcraft: brightness of minecraft's torches, lava and glowstone (0 = off)");
		ConVar hc_torch_light_count("hc_torch_light_count", "4", FCVAR_ARCHIVE,
			"halfcraft: how many of the nearest minecraft lights light half-life's world and characters per pixel, with shadows (up to 4; each renders six shadow maps a frame)");

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

		float srgb_encode(float linear)
		{
			return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
		}

		/// what every face of a point light projects: white, fading out across the seams with its
		/// neighbours (w(u) + w(1/u) = 1 for u the tangent off the face's axis). source reads the cookie
		/// as srgb, so the weights are stored encoded.
		class SeamCookie final : public ITextureRegenerator
		{
		public:
			ITexture* texture()
			{
				if (!texture_) {
					texture_ = materials->CreateProceduralTexture("halfcraft/light_cookie", TEXTURE_GROUP_OTHER, COOKIE_SIZE, COOKIE_SIZE, IMAGE_FORMAT_BGRA8888,
						TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL | TEXTUREFLAGS_SINGLECOPY | TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT);
					texture_->SetTextureRegenerator(this);
					texture_->Download();
				}
				return texture_;
			}

			void release()
			{
				if (texture_) {
					texture_->SetTextureRegenerator(nullptr);
					texture_->DecrementReferenceCount();
					texture_->DeleteIfUnreferenced();
					texture_ = nullptr;
				}
			}

			void RegenerateTextureBits(ITexture*, IVTFTexture* vtf, Rect_t*) override
			{
				if (vtf->Format() != IMAGE_FORMAT_BGRA8888 || vtf->Width() != COOKIE_SIZE || vtf->Height() != COOKIE_SIZE) {
					return;
				}
				std::array<float, COOKIE_SIZE> weights{};
				const float                    tan_half = std::tan(FACE_HALF_FOV * (3.14159265f / 180.0f));
				const float                    log_width = std::log(SEAM_WIDTH);
				for (int i = 0; i < COOKIE_SIZE; ++i) {
					const float u = std::fabs(2.0f * (float(i) + 0.5f) / float(COOKIE_SIZE) - 1.0f) * tan_half;
					weights[i] = u <= 0.0f ? 1.0f : std::clamp(0.5f - 0.5f * std::log(u) / log_width, 0.0f, 1.0f);
				}
				std::uint8_t* out = vtf->ImageData(0, 0, 0);
				for (int y = 0; y < COOKIE_SIZE; ++y) {
					for (int x = 0; x < COOKIE_SIZE; ++x, out += 4) {
						const auto value = static_cast<std::uint8_t>(std::lround(srgb_encode(weights[x] * weights[y]) * 255.0f));
						out[0] = out[1] = out[2] = value;
						out[3] = 255;
					}
				}
			}

			void Release() override {}  // owned here, not by the texture

		private:
			ITexture* texture_ = nullptr;
		};

		SeamCookie g_cookie;

		/// a light that shines every way, made of six of source's projected textures.
		class PointLight
		{
		public:
			/// @param moved - origin or reach changed: which surfaces each face lights has to be found again
			void update(const Vector& origin, float radius, const Vector& color, bool moved)
			{
				static const Vector FORWARD[6] = { Vector(1, 0, 0), Vector(-1, 0, 0), Vector(0, 1, 0), Vector(0, -1, 0), Vector(0, 0, 1), Vector(0, 0, -1) };
				for (int face = 0; face < 6; ++face) {
					const Vector& forward = FORWARD[face];
					const Vector  up = forward.z != 0.0f ? Vector(1, 0, 0) : Vector(0, 0, 1);
					const Vector  right = CrossProduct(forward, up);
					FlashlightState_t state;
					state.m_vecLightOrigin = origin;
					BasisToQuaternion(forward, right, up, state.m_quatOrientation);
					state.m_fHorizontalFOVDegrees = 2.0f * FACE_HALF_FOV;
					state.m_fVerticalFOVDegrees = 2.0f * FACE_HALF_FOV;
					state.m_fConstantAtten = 0.0f;
					state.m_fLinearAtten = radius * CORE_FRACTION;  // saturates to 1 inside the core
					state.m_fQuadraticAtten = 0.0f;
					state.m_Color[0] = color.x;
					state.m_Color[1] = color.y;
					state.m_Color[2] = color.z;
					state.m_Color[3] = 0.0f;
					state.m_NearZ = FACE_NEAR;
					state.m_FarZ = radius;  // fades out from 0.6 of it
					state.m_pSpotlightTexture = g_cookie.texture();
					state.m_nSpotlightTextureFrame = 0;
					state.m_bEnableShadows = true;  // only read when the projected texture is created
					state.m_flShadowDepthBias = SHADOW_DEPTH_BIAS;
					state.m_flShadowSlopeScaleDepthBias = SHADOW_SLOPE_BIAS;
					state.m_flShadowAtten = 0.0f;  // nothing of the light in its shadows
					auto& handle = faces_[face];
					if (handle == CLIENTSHADOW_INVALID_HANDLE) {
						handle = g_pClientShadowMgr->CreateFlashlight(state);
						g_pClientShadowMgr->UpdateProjectedTexture(handle, true);
					} else {
						g_pClientShadowMgr->UpdateFlashlightState(handle, state);
						if (moved) {
							g_pClientShadowMgr->UpdateProjectedTexture(handle, true);
						}
					}
				}
			}

			void destroy()
			{
				for (auto& handle : faces_) {
					if (handle != CLIENTSHADOW_INVALID_HANDLE) {
						g_pClientShadowMgr->DestroyFlashlight(handle);
						handle = CLIENTSHADOW_INVALID_HANDLE;
					}
				}
			}

			[[nodiscard]] bool alive() const { return faces_[0] != CLIENTSHADOW_INVALID_HANDLE; }

		private:
			ClientShadowHandle_t faces_[6] = { CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE,
				CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE };
		};

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

			/// a torch-like light floating at a source position (hc_debug_torch), with or without minecraft.
			void add_debug(const Vector& origin)
			{
				Cluster c;
				c.cell = 0xDEB60000ull + debug_.size();
				c.r = 1.0f, c.g = 165.0f / 255.0f, c.b = 60.0f / 255.0f;
				c.level = 14;
				c.count = 1;
				c.kind = proto::kLightFlame;
				c.origin = origin;
				debug_.push_back(c);
				rebuild_in_ = 0.0f;
			}

			void clear_debug()
			{
				debug_.clear();
				rebuild_in_ = 0.0f;
			}

			/// the projected textures go before the level's shadows do (the shadow manager expects none left).
			void release_lights()
			{
				for (auto& light : points_) {
					light.destroy();
				}
				chosen_.clear();
				placed_.clear();
				rebuild_in_ = 0.0f;
			}

			void shutdown()
			{
				release_lights();
				g_cookie.release();
			}

			void update(float frametime);

		private:
			void rebuild(const Vector& eye, bool with_minecraft, int slot);
			void refresh(float now);

			std::unordered_map<std::uint64_t, std::vector<LightSource>> by_section_;
			std::mutex                                                   hazards_lock_;
			std::unordered_map<std::uint64_t, std::uint8_t>             hazards_;  // block -> proto::BlockHazard
			std::vector<Cluster>                                         debug_;
			std::vector<Cluster>                                         chosen_;  // nearest first
			std::array<PointLight, MAX_POINT_LIGHTS>                     points_;
			std::vector<std::uint64_t>                                   placed_;  // the cluster each point light shows, by slot
			float                                                        rebuild_in_ = 0.0f;
			float                                                        clock_ = 0.0f;
		};

		/// where a light can shine from: minecraft's blocks sink into half-life's walls and floors, so a
		/// torch's centre can end up inside one, where its projected textures would see nothing. the
		/// nearest free spot a little way out along an axis instead.
		Vector out_of_solid(const Vector& origin)
		{
			if (!(UTIL_PointContents(origin) & CONTENTS_SOLID)) {
				return origin;
			}
			static const Vector AXES[6] = { Vector(0, 0, 1), Vector(1, 0, 0), Vector(-1, 0, 0), Vector(0, 1, 0), Vector(0, -1, 0), Vector(0, 0, -1) };
			for (float step = 8.0f; step <= 32.0f; step += 8.0f) {
				for (const auto& axis : AXES) {
					const Vector candidate = origin + axis * step;
					if (!(UTIL_PointContents(candidate) & CONTENTS_SOLID)) {
						return candidate;
					}
				}
			}
			return origin;
		}

		/// how far a light of this level (and this many merged emitters) reaches, in units.
		float reach(int level, int count)
		{
			// a light level reaches about that many blocks in minecraft; a cluster of many a bit further
			const float blocks = float(level + 1) * RADIUS_PER_LEVEL * (1.0f + 0.12f * std::log2(float(std::max(count, 1))));
			return std::min(blocks, MAX_RADIUS_BLOCKS) * static_cast<float>(UNITS_PER_BLOCK);
		}

		void BlockLights::rebuild(const Vector& eye, bool with_minecraft, int slot)
		{
			std::unordered_map<std::uint64_t, Cluster> cells;
			if (with_minecraft) {
				float eye_mc_source[3] = { eye.x, eye.y, eye.z };
				const auto player = source_to_mc(eye_mc_source, slot);
				const double range2 = double(RANGE_BLOCKS) * RANGE_BLOCKS;
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
			}
			chosen_.clear();
			for (auto& entry : cells) {
				Cluster&    c = entry.second;
				const float w = float(c.weight);
				c.r /= w, c.g /= w, c.b /= w;
				c.radius = reach(c.level, c.count);
				// emitters sit a little above the blocks' centre (a torch's flame, lava's surface)
				float origin[3];
				mc_to_source(c.x / c.weight, c.y / c.weight + 0.3, c.z / c.weight, slot, origin);
				c.origin = out_of_solid(Vector(origin[0], origin[1], origin[2]));
				c.score = c.origin.DistTo(eye) - c.radius;
				chosen_.push_back(c);
			}
			for (Cluster c : debug_) {
				c.radius = reach(c.level, c.count);
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
			const int   point_count = std::clamp(hc_torch_light_count.GetInt(), 0, MAX_POINT_LIGHTS);
			placed_.resize(MAX_POINT_LIGHTS, 0);
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
				const float  strength = brightness * (0.35f + 0.65f * float(c.level) / 15.0f) * k;
				const Vector color(std::pow(c.r, COLOUR_DEPTH), std::pow(c.g, COLOUR_DEPTH), std::pow(c.b, COLOUR_DEPTH));
				if (static_cast<int>(i) < point_count) {
					const bool moved = !points_[i].alive() || placed_[i] != c.cell;
					placed_[i] = c.cell;
					points_[i].update(c.origin, c.radius, color * (POINT_INTENSITY * strength), moved);
					continue;
				}
				// the rest light only characters
				dlight_t* light = effects->CL_AllocElight(LIGHT_KEY + static_cast<int>(i));
				if (!light) {
					continue;
				}
				light->origin = c.origin;
				light->radius = c.radius;
				VectorToColorRGBExp32(color * (ELIGHT_INTENSITY * strength), light->color);
				light->die = now + KEEP_ALIVE_SECONDS;
				light->decay = 0.0f;
				light->minlight = 0.0f;
				light->style = 0;
				light->flags = 0;
			}
			for (std::size_t i = std::min<std::size_t>(chosen_.size(), point_count); i < points_.size(); ++i) {
				points_[i].destroy();
			}
		}

		void BlockLights::update(float frametime)
		{
			clock_ += frametime;
			auto&         s = client_session();
			C_BasePlayer* player = C_BasePlayer::GetLocalPlayer();
			const bool    with_minecraft = s.have_mc && s.mc_in_world;
			const bool    active = player && engine->IsInGame() && !s.loading && hc_torch_light.GetFloat() > 0.0f && (with_minecraft || !debug_.empty());
			if (!active) {
				if (!chosen_.empty() || points_[0].alive()) {
					release_lights();  // elights die by themselves without refreshes
				}
				return;
			}
			rebuild_in_ -= frametime;
			if (rebuild_in_ <= 0.0f) {
				rebuild_in_ = REBUILD_SECONDS;
				rebuild(player->EyePosition(), with_minecraft, s.slot);
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
			void LevelShutdownPreEntity() override { block_lights().release_lights(); }
			void Shutdown() override { block_lights().shutdown(); }
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

CON_COMMAND(hc_debug_torch, "halfcraft: a torch light where you look, without minecraft (hc_debug_torch 0 removes them)")
{
	if (args.ArgC() > 1 && atoi(args[1]) == 0) {
		halfcraft::block_lights().clear_debug();
		return;
	}
	C_BasePlayer* player = C_BasePlayer::GetLocalPlayer();
	if (!player) {
		return;
	}
	Vector forward;
	AngleVectors(player->EyeAngles(), &forward);
	trace_t trace;
	UTIL_TraceLine(player->EyePosition(), player->EyePosition() + forward * 2000.0f, MASK_SOLID, player, COLLISION_GROUP_NONE, &trace);
	// a little off the surface, where a torch's flame would be
	halfcraft::block_lights().add_debug(trace.endpos - forward * 16.0f + Vector(0.0f, 0.0f, 8.0f));
}

namespace halfcraft
{
	void client_shadow_depth_view(const CViewSetup& view)
	{
		static IMaterial* material = [] {
			auto* values = new KeyValues("DepthWrite");
			values->SetInt("$no_fullbright", 1);
			values->SetInt("$alphatest", 0);
			values->SetInt("$nocull", 1);
			IMaterial* created = materials->FindProceduralMaterial("__halfcraft_shadow_cap", TEXTURE_GROUP_OTHER, values);
			created->IncrementReferenceCount();
			if (!created->IsPrecached()) {
				materials->CacheUsedMaterials();  // as make_unlit_material does: drawn before any level load cached it
			}
			return created;
		}();
		Vector forward, right, up;
		AngleVectors(view.angles, &forward, &right, &up);
		const float  distance = view.zFar * SHADOW_CAP_FRACTION;
		const float  half = distance * std::tan(DEG2RAD(view.fov * 0.5f)) * 1.1f;  // a bit past the frustum's edges
		const Vector centre = view.origin + forward * distance;
		CMatRenderContextPtr context(materials);
		context->Bind(material);
		IMesh*       mesh = context->GetDynamicMesh(true);
		CMeshBuilder builder;
		builder.Begin(mesh, MATERIAL_QUADS, 1);
		const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
		for (const auto& corner : corners) {
			builder.Position3fv((centre + right * (corner[0] * half) + up * (corner[1] * half)).Base());
			builder.AdvanceVertex();
		}
		builder.End();
		mesh->Draw();
	}
}

// server.dll's way to minecraft's fire, lava and magma (hc_bridge.h)
extern "C" __declspec(dllexport) int HalfCraft_HazardAt(int x, int y, int z)
{
	return halfcraft::block_lights().hazard_at(x, y, z);
}
