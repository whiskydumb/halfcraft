// client.dll: minecraft's light-emitting blocks as source lights (see hc_block_lights.h).

#include "cbase.h"
#include "c_baseplayer.h"
#include "dlight.h"
#include "iclientshadowmgr.h"
#include "iefx.h"
#include "iviewrender.h"
#include "KeyValues.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/itexture.h"
#include "texture_group_names.h"
#include "view.h"
#include "view_shared.h"
#include "vtf/vtf.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
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
		constexpr int   MAX_POINT_LIGHTS = SHADOWED_LIGHTS;  // twelve projected textures each, with shadow maps (hc_hooks.h)
		constexpr int   MAX_LIGHTS = 24;          // the nearest minecraft lights: the point lights among them, and an elight each for characters
		constexpr float RANGE_BLOCKS = 48.0f;     // emitters further than this from the player stay dark
		constexpr float MAX_RADIUS_BLOCKS = 14.0f;
		constexpr float RADIUS_PER_LEVEL = 0.8f;  // blocks of reach per minecraft light level
		constexpr float CORE_FRACTION = 0.2f;     // full brightness out to this part of the reach, then 1/distance
		constexpr float REBUILD_SECONDS = 0.25f;
		constexpr float KEEP_ALIVE_SECONDS = 0.2f;  // source drops an elight we stop refreshing after this
		constexpr int   LIGHT_KEY = 0x48430000;     // elight keys: past any entity index
		constexpr float POINT_INTENSITY = 1.3f;     // a level 15 point light's core, before hc_torch_light (source's flashlight is 1)
		constexpr float COLOUR_DEPTH = 2.2f;        // minecraft's light colours raised to this: flame-coloured light, not a washed-out white
		constexpr float ELIGHT_INTENSITY = 0.6f;    // the same for characters: every light reaches them as an elight
		// the point lights go to the lights whose reach is on screen (assign_slots), and swap without popping
		constexpr float VIEW_MARGIN_DEGREES = 10.0f;  // past the screen's corners, so turning doesn't wait for a light
		constexpr float HOLD_UNITS = 64.0f;           // a light keeps its point light against one this much nearer
		constexpr float FADE_IN_SECONDS = 0.2f;
		constexpr float FADE_OUT_SECONDS = 0.25f;

		// a point light's faces are projected textures (source's flashlight) looking down the cube's axes.
		// each sees a little past 90 degrees, and its cookie fades out across the seam so that two
		// neighbours add up to one. they all cast shadows: besides walls stopping the light, the shadow test
		// is what keeps a face off what's behind it (source's shaders don't clip, so a surface reaching into
		// a face's frustum would also be lit mirrored behind the light)
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
		constexpr float WALL_CLEARANCE = FACE_NEAR + 6.0f;  // units (clear_of_walls)
		// the open faces' light passes walls, so it only reaches this far: round a corner near the light,
		// not through a wall into the next room
		constexpr float WRAP_REACH_BLOCKS = 4.0f;

		ConVar          hc_torch_light("hc_torch_light", "1", FCVAR_ARCHIVE, "halfcraft: brightness of minecraft's torches, lava and glowstone (0 = off)");
		ConVar          hc_torch_light_wrap("hc_torch_light_wrap", "0.35", FCVAR_ARCHIVE,
			"halfcraft: the part of the nearest lights' brightness that half-life's walls don't stop within 4 blocks of them (0 = hard shadows, 1 = none; above 0 adds six faces to each)");
		ConVar          hc_torch_light_pvs("hc_torch_light_pvs", "1", 0, "halfcraft: only lights in the camera's potentially visible set get shadowed point lights");
		constexpr float PVS_EXTENT = 24.0f;  // units around a light's origin that have to be in the camera's pvs
		ConVar          hc_torch_light_count("hc_torch_light_count", "4", FCVAR_ARCHIVE,
			"halfcraft: how many minecraft lights (the ones on screen first, then the nearest) light half-life's world per pixel, with shadows (up to 4; each renders up to twelve shadow maps a frame)");

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
			int           top = INT_MIN;                    // the highest emitter's block y
			float         r = 0, g = 0, b = 0;
			int           level = 0, count = 0;
			std::uint8_t  kind = proto::kLightSteady;
			float         radius = 0;  // units
			float         score = 0;   // nearest first
			bool          visible = true;  // in the camera's pvs: only those get the expensive shadowed point lights
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

		IMaterial* g_shadow_cap = nullptr;  // client_shadow_depth_view's depth-only material

		/// the engine clips each projected texture's lighting to a screen rectangle around its frustum
		/// (r_flashlightscissor). it gets that rectangle wrong for our wide faces whenever one reaches
		/// past the camera: the light ended in straight horizontal and vertical edges across the screen.
		void disable_flashlight_scissor()
		{
			ConVarRef scissor("r_flashlightscissor");
			if (scissor.IsValid() && scissor.GetBool()) {
				scissor.SetValue(0);
			}
		}

		/// the size of the shadow depth textures (the shaders scale the shadow filter by it).
		float shadow_map_resolution()
		{
			static ConVarRef resolution("r_flashlightdepthres");
			return resolution.IsValid() ? resolution.GetFloat() : 1024.0f;
		}

		/// made at a level load: creating and caching a material in the middle of rendering a shadow
		/// map upsets the frame being drawn.
		void prepare_shadow_cap()
		{
			if (g_shadow_cap) {
				return;
			}
			auto* values = new KeyValues("DepthWrite");
			values->SetInt("$no_fullbright", 1);
			values->SetInt("$alphatest", 0);
			values->SetInt("$nocull", 1);
			g_shadow_cap = materials->FindProceduralMaterial("__halfcraft_shadow_cap", TEXTURE_GROUP_OTHER, values);
			g_shadow_cap->IncrementReferenceCount();
			if (!g_shadow_cap->IsPrecached()) {
				materials->CacheUsedMaterials();
			}
		}

		/// the faces whose shadow depth textures hold nothing but the cap (client_shadow_depth_scene), by
		/// ClientShadowHandle_t.
		std::bitset<1 << 16> g_open_faces;
		/// every face of the point lights, by ClientShadowHandle_t: they light the world only
		/// (client_flashlight_lights_models).
		std::bitset<1 << 16> g_block_faces;
		bool                 g_creating_face = false;  // CreateFlashlight projects the new face before it returns its handle
		bool                 g_depth_scene = true;  // the shadow depth texture being drawn gets the scene

		/// a light that shines every way, made of source's projected textures: six faces whose light
		/// half-life's walls stop, and six whose light they don't (hc_torch_light_wrap of it), so that what
		/// a light can't see directly isn't left pitch black. minecraft's light floods around corners too.
		class PointLight
		{
		public:
			/// @param wrap - the part of the light that walls don't stop
			/// @param moved - another cluster took the light over: which surfaces each face lights has to be found again
			void update(const Vector& origin, float radius, const Vector& color, float wrap, bool moved)
			{
				// the engine takes a face's world-to-texture matrix (and which surfaces it lights) only from
				// UpdateProjectedTexture, while its shadow map is drawn from the current state every frame. a
				// reach or origin changed without it (lava spreading, a cluster's centre moving) compared
				// every pixel against a shadow map of another frustum: whole faces in shadow
				if (origin != last_origin_ || radius != last_radius_) {
					last_origin_ = origin;
					last_radius_ = radius;
					moved = true;
				}
				wrap = std::clamp(wrap, 0.0f, 1.0f);
				const float wrap_reach = std::min(radius, WRAP_REACH_BLOCKS * static_cast<float>(UNITS_PER_BLOCK));
				for (int face = 0; face < 6; ++face) {
					place(walled_[face], face, wrap < 1.0f, false, origin, radius, radius, color * (1.0f - wrap), moved);
					place(open_[face], face, wrap > 0.0f, true, origin, radius, wrap_reach, color * wrap, moved);
				}
			}

			void destroy()
			{
				for (auto& handle : walled_) {
					release(handle);
				}
				for (auto& handle : open_) {
					release(handle);
				}
			}

			[[nodiscard]] bool alive() const
			{
				const auto valid = [](ClientShadowHandle_t h) { return h != CLIENTSHADOW_INVALID_HANDLE; };
				return std::any_of(std::begin(walled_), std::end(walled_), valid) || std::any_of(std::begin(open_), std::end(open_), valid);
			}

		private:
			static void release(ClientShadowHandle_t& handle)
			{
				if (handle != CLIENTSHADOW_INVALID_HANDLE) {
					g_open_faces.reset(handle);
					g_block_faces.reset(handle);
					g_pClientShadowMgr->DestroyFlashlight(handle);
					handle = CLIENTSHADOW_INVALID_HANDLE;
				}
			}

			/// creates, updates or (not wanted) removes one face.
			/// @param open - walls don't stop its light (its shadow map holds only the cap)
			/// @param radius - the light's reach, which sets how bright it is with distance
			/// @param reach - how far this face lights (it fades out from 0.6 of it)
			static void place(ClientShadowHandle_t& handle, int face, bool wanted, bool open, const Vector& origin, float radius, float reach, const Vector& color,
				bool moved)
			{
				if (!wanted) {
					release(handle);
					return;
				}
				static const Vector FORWARD[6] = { Vector(1, 0, 0), Vector(-1, 0, 0), Vector(0, 1, 0), Vector(0, -1, 0), Vector(0, 0, 1), Vector(0, 0, -1) };
				const Vector&       forward = FORWARD[face];
				const Vector        up = forward.z != 0.0f ? Vector(1, 0, 0) : Vector(0, 0, 1);
				const Vector        right = CrossProduct(forward, up);
				FlashlightState_t   state;
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
				state.m_FarZ = reach;
				state.m_pSpotlightTexture = g_cookie.texture();
				state.m_nSpotlightTextureFrame = 0;
				state.m_bEnableShadows = true;  // only read when the projected texture is created
				state.m_flShadowDepthBias = SHADOW_DEPTH_BIAS;
				state.m_flShadowSlopeScaleDepthBias = SHADOW_SLOPE_BIAS;
				state.m_flShadowAtten = 0.0f;  // nothing of the light in its shadows
				state.m_flShadowMapResolution = shadow_map_resolution();
				if (handle == CLIENTSHADOW_INVALID_HANDLE) {
					g_creating_face = true;
					handle = g_pClientShadowMgr->CreateFlashlight(state);
					g_creating_face = false;
					g_open_faces.set(handle, open);
					g_block_faces.set(handle);
					g_pClientShadowMgr->UpdateProjectedTexture(handle, true);
				} else {
					g_pClientShadowMgr->UpdateFlashlightState(handle, state);
					if (moved) {
						g_pClientShadowMgr->UpdateProjectedTexture(handle, true);
					}
				}
			}

			float                last_radius_ = 0.0f;
			Vector               last_origin_ = vec3_origin;
			ClientShadowHandle_t walled_[6] = { CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE,
				CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE };
			ClientShadowHandle_t open_[6] = { CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE, CLIENTSHADOW_INVALID_HANDLE,
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
				const auto                  key = pack(header.sx, header.sy, header.sz);
				const auto                  count = std::min<std::uint64_t>(header.count, (bytes - sizeof(header)) / sizeof(proto::RenLight));
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
				const auto                  it = hazards_.find(pack(x, y, z));
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
				slots_.fill({});
				rebuild_in_ = 0.0f;
			}

			void shutdown()
			{
				release_lights();
				g_cookie.release();
			}

			BlockLightStats stats() const
			{
				BlockLightStats stats;
				for (const auto& section : by_section_) {
					stats.emitters += static_cast<std::uint32_t>(section.second.size());
				}
				stats.lights = static_cast<std::uint32_t>(chosen_.size());
				for (const auto& light : points_) {
					stats.shadowed += light.alive() ? 1 : 0;
				}
				return stats;
			}

			void update(float frametime);

			void dump() const;

		private:
			/// what one point light shows: a copy of its cluster, which it keeps while fading out after
			/// the cluster left (or went out of reach)
			struct Slot
			{
				Cluster cluster;
				bool    used = false;
				bool    shown = false;  // the point light holds this cluster's frusta
				float   fade = 0.0f;    // 0..1 of its brightness
			};

			void rebuild(const Vector& eye, bool with_minecraft, MapSlot slot);
			void assign_slots(int count, float frametime);
			void refresh(float now, float frametime);

			std::unordered_map<std::uint64_t, std::vector<LightSource>> by_section_;
			std::mutex                                                  hazards_lock_;
			std::unordered_map<std::uint64_t, std::uint8_t>             hazards_;  // block -> proto::BlockHazard
			std::vector<Cluster>                                        debug_;
			std::vector<Cluster>                                        chosen_;  // nearest first
			std::array<PointLight, MAX_POINT_LIGHTS>                    points_;
			std::array<Slot, MAX_POINT_LIGHTS>                          slots_;  // by point light
			float                                                       rebuild_in_ = 0.0f;
			float                                                       clock_ = 0.0f;
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

		/// how far half-life's walls are from a point along an axis, up to a limit.
		float wall_distance(const Vector& from, const Vector& axis, float limit)
		{
			trace_t trace;
			UTIL_TraceLine(from, from + axis * limit, MASK_SOLID_BRUSHONLY, nullptr, COLLISION_GROUP_NONE, &trace);
			return trace.startsolid ? 0.0f : trace.fraction * limit;
		}

		/// a light keeps WALL_CLEARANCE from half-life's walls and floors where there's room: whatever is
		/// nearer than a face's near plane neither casts shadows nor stops the light (a lantern 4 units
		/// from a wall shone through it), and a light in a surface's plane doesn't light it at all
		/// (torches sunk into the canal's floor had their flames right in it). in a gap narrower than
		/// twice that it sits in the middle.
		Vector clear_of_walls(Vector origin)
		{
			static const Vector AXES[3] = { Vector(1, 0, 0), Vector(0, 1, 0), Vector(0, 0, 1) };
			// a point right in a surface's plane traces as inside it: off it first (minecraft's grid puts
			// torch flames exactly at the height of half-life's floors that sit on it)
			if (wall_distance(origin, AXES[2], 1.0f) == 0.0f) {
				for (const float step : { 1.0f, 2.0f, 4.0f }) {
					const Vector candidates[6] = { origin + AXES[2] * step, origin - AXES[2] * step, origin + AXES[0] * step, origin - AXES[0] * step,
						origin + AXES[1] * step, origin - AXES[1] * step };
					const auto   clear = std::find_if(std::begin(candidates), std::end(candidates), [](const Vector& p) { return wall_distance(p, AXES[2], 1.0f) > 0.0f; });
					if (clear != std::end(candidates)) {
						origin = *clear;
						break;
					}
				}
			}
			for (const auto& axis : AXES) {
				const float ahead = wall_distance(origin, axis, 2.0f * WALL_CLEARANCE);
				const float behind = wall_distance(origin, -axis, 2.0f * WALL_CLEARANCE);
				if (ahead + behind < 2.0f * WALL_CLEARANCE) {
					origin += axis * ((ahead - behind) * 0.5f);
				} else if (ahead < WALL_CLEARANCE) {
					origin -= axis * (WALL_CLEARANCE - ahead);
				} else if (behind < WALL_CLEARANCE) {
					origin += axis * (WALL_CLEARANCE - behind);
				}
			}
			return origin;
		}

		/// hc_debug_light_dump: what the point lights are and where.
		void BlockLights::dump() const
		{
			Msg("halfcraft lights: %d chosen\n", static_cast<int>(chosen_.size()));
			static const Vector AXES[6] = { Vector(1, 0, 0), Vector(-1, 0, 0), Vector(0, 1, 0), Vector(0, -1, 0), Vector(0, 0, 1), Vector(0, 0, -1) };
			for (std::size_t i = 0; i < chosen_.size(); ++i) {
				const Cluster& c = chosen_[i];
				char           walls[128] = {};
				for (int f = 0; f < 6; ++f) {
					const float d = wall_distance(c.origin, AXES[f], c.radius);
					Q_snprintf(walls + Q_strlen(walls), sizeof(walls) - Q_strlen(walls), " %.0f", d);
				}
				Msg("  %2d: kind %d level %2d count %3d origin %.1f %.1f %.1f radius %.0f %s walls(+x -x +y -y +z -z)%s\n", static_cast<int>(i), c.kind, c.level,
					c.count, c.origin.x, c.origin.y, c.origin.z, c.radius, c.visible ? "in pvs" : "hidden", walls);
			}
			for (std::size_t i = 0; i < points_.size(); ++i) {
				Msg("  point light %d: %s, cluster %llx at %.2f\n", static_cast<int>(i), points_[i].alive() ? "on" : "off", slots_[i].used ? slots_[i].cluster.cell : 0ull,
					slots_[i].fade);
			}
		}

		/// how far a light of this level (and this many merged emitters) reaches, in units.
		float reach(int level, int count)
		{
			// a light level reaches about that many blocks in minecraft; a cluster of many a bit further
			const float blocks = float(level + 1) * RADIUS_PER_LEVEL * (1.0f + 0.12f * std::log2(float(std::max(count, 1))));
			return std::min(blocks, MAX_RADIUS_BLOCKS) * static_cast<float>(UNITS_PER_BLOCK);
		}

		void BlockLights::rebuild(const Vector& eye, bool with_minecraft, MapSlot slot)
		{
			std::unordered_map<std::uint64_t, Cluster> cells;
			if (with_minecraft) {
				float        eye_mc_source[3] = { eye.x, eye.y, eye.z };
				const auto   player = source_to_mc(eye_mc_source, slot);
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
						c.top = std::max<int>(c.top, e.y);
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
				// emitters sit a little above the blocks' centre (a torch's flame). lava shines from just above
				// its top surface: a pool's middle is down in half-life's floor and stairs, which its blocks
				// sink into
				const double y = c.kind == proto::kLightLava ? c.top + 1.0 : c.y / c.weight + 0.3;
				float        origin[3];
				mc_to_source(c.x / c.weight, y, c.z / c.weight, slot, origin);
				c.origin = out_of_solid(Vector(origin[0], origin[1], origin[2]));
				c.score = c.origin.DistTo(eye) - c.radius;
				chosen_.push_back(c);
			}
			for (Cluster c : debug_) {
				c.radius = reach(c.level, c.count);
				c.score = -1.0e9f;  // hc_debug_torch's come first, whatever minecraft has around
				chosen_.push_back(c);
			}
			const auto keep = std::min<std::size_t>(chosen_.size(), MAX_LIGHTS);
			std::partial_sort(chosen_.begin(), chosen_.begin() + keep, chosen_.end(), [](const Cluster& a, const Cluster& b) { return a.score < b.score; });
			chosen_.resize(keep);
			const bool pvs_only = hc_torch_light_pvs.GetBool();
			for (Cluster& c : chosen_) {
				c.origin = clear_of_walls(c.origin);
				// a light whose spot the camera's cluster can't see lights little on screen: the shadowed
				// point lights (twelve shadow maps each) go to the ones it can
				const Vector extent(PVS_EXTENT, PVS_EXTENT, PVS_EXTENT);
				c.visible = !pvs_only || engine->IsBoxInViewCluster(c.origin - extent, c.origin + extent);
			}
		}

		/// whether a light's reach shows on screen: its sphere reaches into a cone around the camera's
		/// view, as wide as the screen's diagonal and a little more.
		bool in_view(const Vector& origin, float radius)
		{
			const Vector to = origin - MainViewOrigin();
			const float  distance = to.Length();
			if (distance <= radius) {
				return true;  // the camera is within its reach
			}
			const CViewSetup* setup = view->GetPlayerViewSetup();
			const float       aspect = setup && setup->height > 0 ? float(setup->width) / float(setup->height) : 16.0f / 9.0f;
			const float       tan_x = std::tan(DEG2RAD((setup ? setup->fov : 90.0f) * 0.5f));
			const float       tan_y = tan_x / aspect;
			const float       half_diagonal = std::atan(std::sqrt(tan_x * tan_x + tan_y * tan_y)) + DEG2RAD(VIEW_MARGIN_DEGREES);
			const float       off_axis = std::acos(std::clamp(DotProduct(to, MainViewForward()) / distance, -1.0f, 1.0f));
			return off_axis - std::asin(radius / distance) < half_diagonal;
		}

		/// a light's brightness now (0..), before hc_torch_light's intensities and the colour.
		float strength_of(const Cluster& c, float clock, float brightness)
		{
			// flames flicker, lava glows slowly, the rest stay put
			const float t = clock + float(c.cell % 997) * 0.37f;
			float       k = 1.0f;
			if (c.kind == proto::kLightFlame) {
				k = 1.0f + 0.07f * std::sin(t * 9.1f) + 0.05f * std::sin(t * 23.7f + 1.3f) + 0.03f * std::sin(t * 4.3f + 0.7f);
			} else if (c.kind == proto::kLightLava) {
				k = 1.0f + 0.08f * std::sin(t * 1.3f) + 0.03f * std::sin(t * 3.1f + 2.0f);
			}
			// brighter emitters shine harder as well as further
			return brightness * (0.35f + 0.65f * float(c.level) / 15.0f) * k;
		}

		Vector colour_of(const Cluster& c)
		{
			return Vector(std::pow(c.r, COLOUR_DEPTH), std::pow(c.g, COLOUR_DEPTH), std::pow(c.b, COLOUR_DEPTH));
		}

		/// which clusters the point lights show: the ones whose light is on screen first, then the
		/// nearest, and a light keeps its cluster against one only a little nearer. a point light whose
		/// cluster lost out fades out before it takes the next, and a new one fades in, so they don't pop.
		void BlockLights::assign_slots(int count, float frametime)
		{
			struct Candidate
			{
				const Cluster* cluster;
				bool           shown;
				float          score;
			};
			std::vector<Candidate> candidates;
			for (const Cluster& c : chosen_) {
				if (!c.visible) {
					continue;
				}
				const bool held = std::any_of(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.used && s.cluster.cell == c.cell; });
				candidates.push_back({ &c, in_view(c.origin, c.radius), c.score - (held ? HOLD_UNITS : 0.0f) });
			}
			std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.shown != b.shown ? a.shown : a.score < b.score; });
			if (static_cast<int>(candidates.size()) > count) {
				candidates.resize(count);
			}
			for (Slot& s : slots_) {
				if (!s.used) {
					continue;
				}
				const auto it = std::find_if(candidates.begin(), candidates.end(), [&](const Candidate& c) { return c.cluster->cell == s.cluster.cell; });
				if (it != candidates.end()) {
					s.cluster = *it->cluster;
					s.fade = std::min(1.0f, s.fade + frametime / FADE_IN_SECONDS);
					candidates.erase(it);
				} else {
					s.fade -= frametime / FADE_OUT_SECONDS;
					if (s.fade <= 0.0f) {
						s = {};
					}
				}
			}
			for (const Candidate& c : candidates) {
				const auto free = std::find_if(slots_.begin(), slots_.end(), [](const Slot& s) { return !s.used; });
				if (free == slots_.end()) {
					break;  // it waits for a point light to fade out
				}
				*free = { *c.cluster, true, false, 0.0f };
			}
		}

		void BlockLights::refresh(float now, float frametime)
		{
			const float brightness = std::max(0.0f, hc_torch_light.GetFloat());
			assign_slots(std::clamp(hc_torch_light_count.GetInt(), 0, MAX_POINT_LIGHTS), frametime);
			for (std::size_t i = 0; i < slots_.size(); ++i) {
				Slot& s = slots_[i];
				if (!s.used) {
					points_[i].destroy();
					continue;
				}
				const float strength = strength_of(s.cluster, clock_, brightness) * s.fade;
				points_[i].update(s.cluster.origin, s.cluster.radius, colour_of(s.cluster) * (POINT_INTENSITY * strength), hc_torch_light_wrap.GetFloat(),
					!s.shown || !points_[i].alive());
				s.shown = true;
			}
			// characters get every light as an elight: the point lights' faces light only the world
			for (std::size_t i = 0; i < chosen_.size(); ++i) {
				const Cluster& c = chosen_[i];
				dlight_t*      light = effects->CL_AllocElight(LIGHT_KEY + static_cast<int>(i));
				if (!light) {
					continue;
				}
				light->origin = c.origin;
				light->radius = c.radius;
				VectorToColorRGBExp32(colour_of(c) * (ELIGHT_INTENSITY * strength_of(c, clock_, brightness)), light->color);
				light->die = now + KEEP_ALIVE_SECONDS;
				light->decay = 0.0f;
				light->minlight = 0.0f;
				light->style = 0;
				light->flags = 0;
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
				if (!chosen_.empty() || std::any_of(points_.begin(), points_.end(), [](const PointLight& p) { return p.alive(); })) {
					release_lights();  // elights die by themselves without refreshes
				}
				return;
			}
			rebuild_in_ -= frametime;
			if (rebuild_in_ <= 0.0f) {
				rebuild_in_ = REBUILD_SECONDS;
				rebuild(player->EyePosition(), with_minecraft, s.slot);
			}
			refresh(gpGlobals->curtime, frametime);
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
			void LevelInitPostEntity() override
			{
				disable_flashlight_scissor();
				prepare_shadow_cap();
			}
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

	BlockLightStats block_lights_stats()
	{
		return block_lights().stats();
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
	void client_shadow_depth_begin(unsigned short shadow)
	{
		g_depth_scene = !g_open_faces.test(shadow);
	}

	bool client_shadow_depth_scene()
	{
		return g_depth_scene;
	}

	bool client_flashlight_lights_models(unsigned short shadow)
	{
		return !g_creating_face && !g_block_faces.test(shadow);
	}
}

CON_COMMAND(hc_debug_light_dump, "halfcraft (debug): list the block lights")
{
	halfcraft::block_lights().dump();
}

namespace halfcraft
{
	void client_shadow_depth_view(const CViewSetup& depth_view)
	{
		if (!g_shadow_cap) {
			return;
		}
		Vector forward, right, up;
		AngleVectors(depth_view.angles, &forward, &right, &up);
		const float          distance = depth_view.zFar * SHADOW_CAP_FRACTION;
		const float          half = distance * std::tan(DEG2RAD(depth_view.fov * 0.5f)) * 1.1f;  // a bit past the frustum's edges
		const Vector         centre = depth_view.origin + forward * distance;
		CMatRenderContextPtr context(materials);
		// world space: whatever was drawn last may have left its own model matrix behind
		context->MatrixMode(MATERIAL_MODEL);
		context->PushMatrix();
		context->LoadIdentity();
		context->Bind(g_shadow_cap);
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
		context->MatrixMode(MATERIAL_MODEL);
		context->PopMatrix();
	}
}

// server.dll's way to minecraft's fire, lava and magma (hc_bridge.h)
extern "C" __declspec(dllexport) int HalfCraft_HazardAt(int x, int y, int z)
{
	return halfcraft::block_lights().hazard_at(x, y, z);
}
