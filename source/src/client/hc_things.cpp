// client.dll: what minecraft shows besides its blocks (see hc_things.h). ported from SkyCraft's
// WorldRender.cpp (BuildEntities, BuildOutline, OnTexture, OnMesh).

#include "cbase.h"
#include "c_baseanimating.h"
#include "cliententitylist.h"
#include "clientleafsystem.h"
#include "model_types.h"
#include "view.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "client/hc_blocks.h"
#include "client/hc_client.h"
#include "client/hc_light.h"
#include "client/hc_mesh.h"
#include "client/hc_texture.h"
#include "client/hc_things.h"
#include "core/hc_log.h"
#include "core/hc_units.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr float        PI = 3.14159265f;
		constexpr float        DEGREES = PI / 180.0f;
		constexpr auto         SCENE_TIMEOUT = std::chrono::milliseconds(1000);  // a scene this old is minecraft gone quiet: hide it
		constexpr float        OUTLINE_GROW = 0.002f;                            // blocks: the outline sits just outside the block
		constexpr float        OUTLINE_WIDTH = 0.0025f;                          // per unit of distance: about 2 pixels at 1080p
		constexpr std::uint8_t OUTLINE_COLOR[4] = { 0, 0, 0, 0x66 };             // minecraft's: black, 40%
		constexpr std::uint8_t WHITE[4] = { 255, 255, 255, 255 };

		// a list of triangles with the materials to draw them with, built on the cpu each frame.
		class DrawList
		{
		public:
			void clear()
			{
				vertices_.clear();
				batches_.clear();
			}

			/// what the following vertices are drawn with (consecutive calls with the same pair share a batch).
			void use(IMaterial* material, bool translucent)
			{
				if (batches_.empty() || batches_.back().material != material || batches_.back().translucent != translucent) {
					batches_.push_back({ material, translucent, static_cast<int>(vertices_.size()), 0 });
				}
			}

			void push(const DrawVertex& vertex)
			{
				vertices_.push_back(vertex);
				++batches_.back().count;
			}

			void draw(bool translucent) const
			{
				for (const auto& batch : batches_) {
					if (batch.translucent == translucent) {
						draw_triangles(batch.material, vertices_.data() + batch.first, batch.count);
					}
				}
			}

			[[nodiscard]] bool empty() const { return vertices_.empty(); }

			void grow_bounds(Vector& lo, Vector& hi) const
			{
				for (const auto& v : vertices_) {
					for (int k = 0; k < 3; ++k) {
						lo[k] = std::min(lo[k], v.position[k]);
						hi[k] = std::max(hi[k], v.position[k]);
					}
				}
			}

		private:
			struct Batch
			{
				IMaterial* material;
				bool       translucent;
				int        first;
				int        count;
			};
			std::vector<DrawVertex> vertices_;
			std::vector<Batch>      batches_;
		};

		// minecraft-space geometry around a point, into a draw list in source space.
		struct Emitter
		{
			DrawList& list;
			int       slot;
			double    base[3];  // minecraft coords the positions are relative to
			float     u_scale, v_scale;

			void vertex(const float p[3], float u, float v, const std::uint8_t color[4])
			{
				DrawVertex out;
				mc_to_source(base[0] + p[0], base[1] + p[1], base[2] + p[2], slot, out.position);
				std::memcpy(out.color, color, 4);
				out.uv[0] = u * u_scale;
				out.uv[1] = v * v_scale;
				list.push(out);
			}

			/// corners listed TL, TR, BR, BL; uv = atlas rect {u0, v0, u1, v1}.
			void quad(const float p[4][3], const float uv[4], const std::uint8_t color[4])
			{
				const float corner_uv[4][2] = { { uv[0], uv[1] }, { uv[2], uv[1] }, { uv[2], uv[3] }, { uv[0], uv[3] } };
				for (int k : { 0, 1, 2, 0, 2, 3 }) {
					vertex(p[k], corner_uv[k][0], corner_uv[k][1], color);
				}
			}
		};

		/// white (or a tint, rgba8 with red in the low byte; 0 = none) in the map's light.
		void lit_white(const Vector& light, float shade, std::uint32_t tint, std::uint8_t out[4])
		{
			std::uint8_t base[4] = { 255, 255, 255, 255 };
			if (tint) {
				base[0] = static_cast<std::uint8_t>(tint & 0xFF);
				base[1] = static_cast<std::uint8_t>((tint >> 8) & 0xFF);
				base[2] = static_cast<std::uint8_t>((tint >> 16) & 0xFF);
			}
			lit_color(base, shade, light, 0, out);
		}

		/// an axis-aligned box turned `yaw` radians about its vertical centre line, with one texture per
		/// face group (sides, top, bottom). shaded: minecraft's face brightness.
		void emit_box(Emitter& out, const float min[3], const float size[3], float yaw, const float side[4], const float top[4], const float bottom[4],
			std::uint32_t top_tint, bool shaded, const Vector& light)
		{
			const float cx = min[0] + size[0] * 0.5f, cz = min[2] + size[2] * 0.5f;
			const float c = std::cos(yaw), s = std::sin(yaw);
			auto        corner = [&](int i, float p[3]) {
                const float lx = ((i & 1) ? 0.5f : -0.5f) * size[0], lz = ((i & 4) ? 0.5f : -0.5f) * size[2];
                p[0] = cx + lx * c - lz * s;
                p[1] = min[1] + ((i & 2) ? size[1] : 0.0f);
                p[2] = cz + lx * s + lz * c;
			};
			// corner bits: 1 = +x, 2 = +y, 4 = +z; each face TL, TR, BR, BL seen from outside
			static constexpr int FACES[6][4] = {
				{ 6, 7, 5, 4 },  // south (+z)
				{ 3, 2, 0, 1 },  // north (-z)
				{ 7, 3, 1, 5 },  // east (+x)
				{ 2, 6, 4, 0 },  // west (-x)
				{ 2, 3, 7, 6 },  // top
				{ 4, 5, 1, 0 },  // bottom
			};
			// FACE_SHADE index (direction ordinal + 1) of each face above
			static constexpr int SHADE_OF[6] = { 4, 3, 6, 5, 2, 1 };
			for (int f = 0; f < 6; ++f) {
				float p[4][3];
				for (int k = 0; k < 4; ++k) {
					corner(FACES[f][k], p[k]);
				}
				std::uint8_t color[4];
				lit_white(light, shaded ? FACE_SHADE[SHADE_OF[f]] : 1.0f, f == 4 ? top_tint : 0, color);
				out.quad(p, f == 4 ? top : f == 5 ? bottom : side, color);
			}
		}

		/// minecraft's arrow (or a trident's icon) at the emitter's base, flying along d (unit length,
		/// minecraft axes). around that axis s is the horizontal side and u the "up"; the fins sit at 45
		/// degrees between them.
		void emit_arrow(Emitter& out, const float d[3], const float side_uv[4], const float back_uv[4], bool trident, const std::uint8_t color[4])
		{
			float s[3] = { d[2], 0.0f, -d[0] };
			float length = std::sqrt(s[0] * s[0] + s[2] * s[2]);
			if (length < 1e-3f) {
				s[0] = 1.0f, s[2] = 0.0f, length = 1.0f;
			}
			s[0] /= length, s[2] /= length;
			const float     u[3] = { s[1] * d[2] - s[2] * d[1], s[2] * d[0] - s[0] * d[2], s[0] * d[1] - s[1] * d[0] };
			constexpr float r = 0.70710678f;
			const float     fins[2][3] = { { (u[0] + s[0]) * r, (u[1] + s[1]) * r, (u[2] + s[2]) * r }, { (u[0] - s[0]) * r, (u[1] - s[1]) * r, (u[2] - s[2]) * r } };
			auto            at = [&](float along, const float* q, float across, const float* q2, float across2, float p[3]) {
                for (int k = 0; k < 3; ++k) {
                    p[k] = d[k] * along + q[k] * across + (q2 ? q2[k] * across2 : 0.0f);
                }
			};
			if (!trident) {
				// minecraft's ArrowModel (1/16 block units, scaled 0.9): two fins 16 long and 4 wide from
				// x -12 (fletching) to +4 (head), and a 4x4 back plate at x -11.
				constexpr float k = 0.9f / 16.0f;
				for (const auto& q : fins) {
					float p[4][3];
					at(-12 * k, q, -2 * k, nullptr, 0, p[0]);
					at(4 * k, q, -2 * k, nullptr, 0, p[1]);
					at(4 * k, q, 2 * k, nullptr, 0, p[2]);
					at(-12 * k, q, 2 * k, nullptr, 0, p[3]);
					out.quad(p, side_uv, color);
				}
				float p[4][3];
				at(-11 * k, fins[0], -2 * k, fins[1], -2 * k, p[0]);
				at(-11 * k, fins[0], 2 * k, fins[1], -2 * k, p[1]);
				at(-11 * k, fins[0], 2 * k, fins[1], 2 * k, p[2]);
				at(-11 * k, fins[0], -2 * k, fins[1], 2 * k, p[3]);
				out.quad(p, back_uv, color);
			} else {
				// the item icon, whose diagonal runs handle (bottom-left) to tip (top-right)
				constexpr float h = 0.9f;
				for (const auto& q : fins) {
					float p[4][3];
					at(0, q, h, nullptr, 0, p[0]);
					at(h, q, 0, nullptr, 0, p[1]);
					at(0, q, -h, nullptr, 0, p[2]);
					at(-h, q, 0, nullptr, 0, p[3]);
					out.quad(p, side_uv, color);
				}
			}
		}

		/// minecraft's entity lighting (two fixed lights, ambient 0.4) for a normal in minecraft axes.
		float entity_shade(const float n[3])
		{
			static const auto lights = [] {
				std::array<std::array<float, 3>, 2> l{ { { 0.2f, 1.0f, -0.7f }, { -0.2f, 1.0f, 0.7f } } };
				for (auto& v : l) {
					const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
					for (auto& c : v) {
						c /= length;
					}
				}
				return l;
			}();
			float sum = 0.0f;
			for (const auto& l : lights) {
				sum += std::max(0.0f, n[0] * l[0] + n[1] * l[1] + n[2] * l[2]);
			}
			return std::min(1.0f, 0.4f + 0.6f * sum);
		}

		struct EntityTexture
		{
			McTexture  texture{ "entity" };
			IMaterial* cutout = nullptr;
			IMaterial* translucent = nullptr;

			~EntityTexture()
			{
				release_material(cutout);
				release_material(translucent);
			}
		};

		/// a bone's world transform this frame (setting the bones up if nothing has yet).
		bool bone_matrices(C_BaseAnimating* entity, matrix3x4_t* out)
		{
			C_BaseAnimating::AutoAllowBoneAccess access(true, false);
			return entity->GetModelPtr() && entity->SetupBones(out, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, gpGlobals->curtime);
		}

		// ---- minecraft's arrows stuck in npcs --------------------------------------------------
		// pinned to the hitbox their path passed closest to, so they follow its animation, and its
		// ragdoll when it dies. minecraft itself drops arrows that hit a creature.
		class StuckArrows
		{
		public:
			/// from server.dll's event loop: pinned on the next frame.
			void queue(int entindex, const Vector& hit, const Vector& direction)
			{
				std::lock_guard<std::mutex> lock(lock_);
				pending_.push_back({ entindex, hit, direction });
			}

			void clear()
			{
				std::lock_guard<std::mutex> lock(lock_);
				pending_.clear();
				arrows_.clear();
			}

			/// pins what's queued and draws every arrow where its bone is now.
			void emit(DrawList& list, IMaterial* material, LightCache& light, int slot, const float side_uv[4], const float back_uv[4], float u_scale,
				float v_scale);

		private:
			struct Pending
			{
				int    entindex;
				Vector hit, direction;  // source units / axes
			};

			struct Arrow
			{
				CHandle<C_BaseAnimating> entity;
				int                      model_index;
				int                      bone;
				Vector                   local_position;  // the arrow's origin in the bone's space
				Vector                   local_direction;
				Vector                   last_origin;  // the entity's, to find its ragdoll by
			};

			void              pin(const Pending& pending);
			C_BaseAnimating*  ragdoll_of(const Arrow& arrow, C_BaseAnimating* entity) const;

			static constexpr float  SEARCH_DEPTH = 30.0f;  // units along the path past the hit to look for a hitbox
			static constexpr float  MAX_MISS = 34.0f;      // a path further than this from every hitbox missed the body
			static constexpr float  HEAD_REACH = 9.0f;     // the arrow's origin to its tip (4/16 of 0.9 blocks)
			static constexpr float  HEAD_DEPTH = 2.0f;     // the tip sits this far past the hitbox's centre line
			static constexpr size_t MAX_PER_ENTITY = 16;
			static constexpr size_t MAX_ARROWS = 160;

			std::mutex           lock_;
			std::vector<Pending> pending_;
			std::deque<Arrow>    arrows_;
		};

		void StuckArrows::pin(const Pending& pending)
		{
			C_BaseEntity*    base = ClientEntityList().GetBaseEntity(pending.entindex);
			C_BaseAnimating* entity = base ? base->GetBaseAnimating() : nullptr;
			CStudioHdr*      model = entity ? entity->GetModelPtr() : nullptr;
			mstudiohitboxset_t* set = model ? model->pHitboxSet(entity->GetHitboxSet()) : nullptr;
			matrix3x4_t         bones[MAXSTUDIOBONES];
			if (!set || !bone_matrices(entity, bones)) {
				return;
			}
			int    best = -1;
			float  best_miss = FLT_MAX;
			Vector best_point;
			for (int i = 0; i < set->numhitboxes; ++i) {
				const mstudiobbox_t* box = set->pHitbox(i);
				Vector               centre;
				VectorTransform((box->bbmin + box->bbmax) * 0.5f, bones[box->bone], centre);
				const float  along = std::clamp(DotProduct(centre - pending.hit, pending.direction), 0.0f, SEARCH_DEPTH);
				const Vector closest = pending.hit + pending.direction * along;
				const float  miss = (centre - closest).Length();
				if (miss < best_miss) {
					best_miss = miss;
					best = box->bone;
					best_point = closest;
				}
			}
			if (best < 0 || best_miss > MAX_MISS) {
				return;  // passed beside the body
			}
			// the tip just past the hitbox's centre line, the shaft sticking out
			const Vector origin = best_point - pending.direction * (HEAD_REACH - HEAD_DEPTH);
			Arrow        arrow{ entity, entity->GetModelIndex(), best, {}, {}, entity->GetAbsOrigin() };
			VectorITransform(origin, bones[best], arrow.local_position);
			VectorIRotate(pending.direction, bones[best], arrow.local_direction);

			// the oldest go first
			size_t mine = std::count_if(arrows_.begin(), arrows_.end(), [&](const Arrow& a) { return a.entity == arrow.entity; });
			for (auto it = arrows_.begin(); it != arrows_.end() && mine >= MAX_PER_ENTITY;) {
				if (it->entity == arrow.entity) {
					it = arrows_.erase(it);
					--mine;
				} else {
					++it;
				}
			}
			arrows_.push_back(arrow);
			while (arrows_.size() > MAX_ARROWS) {
				arrows_.pop_front();
			}
		}

		C_BaseAnimating* StuckArrows::ragdoll_of(const Arrow& arrow, C_BaseAnimating* entity) const
		{
			// a dying npc hides and leaves a ragdoll copy of itself (same model) where it fell
			constexpr float NEAR = 80.0f;
			for (C_BaseEntity* other = ClientEntityList().FirstBaseEntity(); other; other = ClientEntityList().NextBaseEntity(other)) {
				C_BaseAnimating* candidate = other->GetBaseAnimating();
				if (candidate && candidate != entity && candidate->IsRagdoll() && candidate->GetModelIndex() == arrow.model_index &&
					(candidate->GetAbsOrigin() - arrow.last_origin).LengthSqr() < NEAR * NEAR) {
					return candidate;
				}
			}
			return nullptr;
		}

		void StuckArrows::emit(DrawList& list, IMaterial* material, LightCache& light, int slot, const float side_uv[4], const float back_uv[4],
			float u_scale, float v_scale)
		{
			{
				std::vector<Pending> pending;
				{
					std::lock_guard<std::mutex> lock(lock_);
					pending.swap(pending_);
				}
				for (const auto& p : pending) {
					pin(p);
				}
			}
			list.use(material, false);
			for (auto it = arrows_.begin(); it != arrows_.end();) {
				C_BaseAnimating* entity = it->entity.Get();
				if (!entity || entity->IsDormant() || entity->IsEffectActive(EF_NODRAW)) {
					if (C_BaseAnimating* ragdoll = ragdoll_of(*it, entity)) {
						it->entity = ragdoll;
						entity = ragdoll;
					} else if (!entity) {
						it = arrows_.erase(it);
						continue;
					} else {
						++it;  // hidden for now
						continue;
					}
				}
				matrix3x4_t bones[MAXSTUDIOBONES];
				if (it->bone >= entity->GetModelPtr()->numbones() || !bone_matrices(entity, bones)) {
					++it;
					continue;
				}
				it->last_origin = entity->GetAbsOrigin();
				Vector position, direction;
				VectorTransform(it->local_position, bones[it->bone], position);
				VectorRotate(it->local_direction, bones[it->bone], direction);

				const float  source_position[3] = { position.x, position.y, position.z };
				const float  source_direction[3] = { direction.x, direction.y, direction.z };
				const McVec  base = source_to_mc(source_position, slot);
				float        mc_direction[3];
				std::uint8_t color[4];
				source_dir_to_mc(source_direction, mc_direction);
				lit_white(light.at(source_position), 1.0f, 0, color);
				Emitter out{ list, slot, { base.x, base.y, base.z }, u_scale, v_scale };
				emit_arrow(out, mc_direction, side_uv, back_uv, false, color);
				++it;
			}
		}

		// ---- soft contact shadows --------------------------------------------------------------
		constexpr int   SHADOW_TEXTURE_SIZE = 32;
		constexpr float SHADOW_REACH = 2.0f;     // blocks: further above the ground than this, no shadow
		constexpr float SHADOW_STRENGTH = 0.5f;  // darkest, right under the feet
		constexpr float SHADOW_LIFT = 0.02f;     // blocks above the ground

		/// a black disc, opaque in the middle and fading out to its rim.
		std::vector<std::uint8_t> shadow_image()
		{
			std::vector<std::uint8_t> rgba(SHADOW_TEXTURE_SIZE * SHADOW_TEXTURE_SIZE * 4, 0);
			const float               half = SHADOW_TEXTURE_SIZE * 0.5f;
			for (int y = 0; y < SHADOW_TEXTURE_SIZE; ++y) {
				for (int x = 0; x < SHADOW_TEXTURE_SIZE; ++x) {
					const float dx = (x + 0.5f - half) / half, dy = (y + 0.5f - half) / half;
					const float d2 = std::min(1.0f, dx * dx + dy * dy);
					rgba[(y * SHADOW_TEXTURE_SIZE + x) * 4 + 3] = static_cast<std::uint8_t>((1.0f - d2) * (1.0f - d2) * 255.0f);
				}
			}
			return rgba;
		}

		/// minecraft y of the ground under a point within SHADOW_REACH blocks: half-life's world or a
		/// minecraft block, whichever is higher; NaN for none.
		double ground_below(double x, double y, double z, int slot)
		{
			double ground = std::nan("");
			float  top[3], bottom[3];
			mc_to_source(x, y + 0.1, z, slot, top);
			mc_to_source(x, y - SHADOW_REACH, z, slot, bottom);
			trace_t trace;
			UTIL_TraceLine(Vector(top[0], top[1], top[2]), Vector(bottom[0], bottom[1], bottom[2]), MASK_SOLID_BRUSHONLY, nullptr, COLLISION_GROUP_NONE,
				&trace);
			if (trace.fraction < 1.0f && !trace.startsolid) {
				ground = trace.endpos.z / UNITS_PER_BLOCK;
			}
			const int bx = static_cast<int>(std::floor(x)), bz = static_cast<int>(std::floor(z));
			const int to = static_cast<int>(std::floor(y - SHADOW_REACH));
			for (int by = static_cast<int>(std::floor(y + 0.1)); by >= to; --by) {
				if (by + 1.0 <= y + 0.1 && blocks_solid(bx, by, bz)) {
					if (std::isnan(ground) || by + 1.0 > ground) {
						ground = by + 1.0;
					}
					break;
				}
			}
			return ground;
		}

		// geometry minecraft's entity renderer drew (proto::RenBatch + proto::RenVertex), positions in
		// blocks relative to some origin
		struct McMesh
		{
			std::vector<proto::RenBatch>  batches;
			std::vector<proto::RenVertex> vertices;

			void clear()
			{
				batches.clear();
				vertices.clear();
			}

			/// the batches and vertices following a message's header; false (and empty) if they don't fit.
			bool read(const std::uint8_t* payload, std::uint32_t bytes, std::size_t head, std::uint32_t batch_count, std::uint32_t vertex_count)
			{
				clear();
				const std::uint64_t need = head + std::uint64_t(batch_count) * sizeof(proto::RenBatch) + std::uint64_t(vertex_count) * sizeof(proto::RenVertex);
				if (batch_count == 0 || vertex_count == 0 || bytes < need) {
					return false;
				}
				const auto* first_batch = reinterpret_cast<const proto::RenBatch*>(payload + head);
				const auto* first_vertex = reinterpret_cast<const proto::RenVertex*>(first_batch + batch_count);
				for (std::uint32_t b = 0; b < batch_count; ++b) {
					if (std::uint64_t(first_batch[b].first) + first_batch[b].count <= vertex_count) {
						batches.push_back(first_batch[b]);
					}
				}
				vertices.assign(first_vertex, first_vertex + vertex_count);
				return true;
			}
		};

		// ---- the renderable -------------------------------------------------------------------
		class Things final : public CDefaultClientRenderable
		{
		public:
			Things() { SetIdentityMatrix(transform_); }

			void on_texture(const std::uint8_t* payload, std::uint32_t bytes);
			void on_scene(const std::uint8_t* payload, std::uint32_t bytes);
			void on_avatar(const std::uint8_t* payload, std::uint32_t bytes);
			/// drops the scene and the entity textures.
			void clear();
			/// once per frame before rendering: this frame's geometry and bounds.
			void update();
			void level_init() { level_loaded_ = true; }
			void level_shutdown();
			void shutdown();
			StuckArrows& stuck_arrows() { return arrows_; }

			// IClientRenderable
			const Vector&      GetRenderOrigin() override { return origin_; }
			const QAngle&      GetRenderAngles() override { return vec3_angle; }
			const matrix3x4_t& RenderableToWorldTransform() override { return transform_; }
			bool               ShouldDraw() override { return visible_; }
			bool               IsTransparent() override { return true; }
			bool               IsTwoPass() override { return true; }
			void               GetRenderBounds(Vector& mins, Vector& maxs) override
			{
				mins = mins_;
				maxs = maxs_;
			}
			int DrawModel(int flags) override;

		private:
			void ensure_materials(const AtlasView& atlas);
			void build_entities(const AtlasView& atlas, LightCache& light, int slot);
			/// a captured mesh at a minecraft origin into a draw list, lit like the blocks.
			void build_mesh(DrawList& list, const McMesh& mesh, const double origin[3], const AtlasView& atlas, LightCache& light, int slot,
				const double eye[3]);
			void emit_shadow(const proto::WorldEntity& e, int slot);
			void draw_outline();
			void set_bounds(const Vector& lo, const Vector& hi);

			proto::WorldEntities                                               entities_{};
			DrawList                                                           entity_list_, scene_list_, avatar_list_;
			std::unordered_map<std::uint32_t, std::unique_ptr<EntityTexture>> textures_;

			// the scene as minecraft sent it (positions relative to its origin), and the player's own
			// body in third person (relative to its feet; empty in first person)
			McMesh                                scene_;
			double                                scene_origin_[3]{};
			bool                                  scene_fresh_ = false;  // arrived since scene_list_ was built
			std::chrono::steady_clock::time_point scene_time_{};
			McMesh                                avatar_;

			IMaterial* crack_material_ = nullptr;
			IMaterial* outline_material_ = nullptr;
			ITexture*  crack_texture_ = nullptr;  // the atlas the crack material samples
			McTexture  shadow_texture_{ "shadow" };
			IMaterial* shadow_material_ = nullptr;

			StuckArrows arrows_;
			float       arrow_uv_[2][4]{};  // side view, back plate: from the last arrow minecraft showed
			bool        have_arrow_uv_ = false;

			bool   selection_ = false;
			Vector selection_lo_, selection_hi_;  // source units

			bool        level_loaded_ = false;
			bool        visible_ = false;
			int         built_frame_ = -1;
			Vector      origin_{ 0.0f, 0.0f, 0.0f };
			Vector      mins_{ 0.0f, 0.0f, 0.0f }, maxs_{ 0.0f, 0.0f, 0.0f };
			matrix3x4_t transform_;
		};

		Things& things()
		{
			static Things instance;
			return instance;
		}

		/// minecraft's camera is detached (F5) and drives the view: the player's body shows.
		bool third_person()
		{
			const auto& s = client_session();
			return s.puppeting && s.pose_valid && s.mc.cameraMode != 0 && s.mc.cameraDistance > 0.0f;
		}

		void Things::on_texture(const std::uint8_t* payload, std::uint32_t bytes)
		{
			if (bytes < sizeof(proto::RenTexture)) {
				return;
			}
			proto::RenTexture header;
			std::memcpy(&header, payload, sizeof(header));
			if (header.id == 0 || header.width == 0 || header.height == 0 || header.width > 4096 || header.height > 4096 ||
				bytes < sizeof(header) + std::uint64_t(header.width) * header.height * 4) {
				log_warning("bad entity texture %u (%ux%u, %u bytes)", header.id, header.width, header.height, bytes);
				return;
			}
			auto& entry = textures_[header.id];
			if (!entry) {
				entry = std::make_unique<EntityTexture>();
			}
			const bool resized = entry->texture.set(payload + sizeof(header), static_cast<int>(header.width), static_cast<int>(header.height));
			if (!entry->cutout) {
				entry->cutout = make_unlit_material("entity_cutout", entry->texture.texture(), Blend::CUTOUT);
				entry->translucent = make_unlit_material("entity_translucent", entry->texture.texture(), Blend::TRANSLUCENT);
			} else if (resized) {
				set_material_texture(entry->cutout, entry->texture.texture());
				set_material_texture(entry->translucent, entry->texture.texture());
			}
			scene_fresh_ = true;  // batches waiting on this texture can show now
			log_info("entity texture %u (%ux%u)", header.id, header.width, header.height);
		}

		void Things::on_scene(const std::uint8_t* payload, std::uint32_t bytes)
		{
			scene_.clear();
			scene_fresh_ = true;
			scene_time_ = std::chrono::steady_clock::now();
			if (bytes < sizeof(proto::RenScene)) {
				return;
			}
			proto::RenScene header;
			std::memcpy(&header, payload, sizeof(header));
			if (!scene_.read(payload, bytes, sizeof(header), header.batchCount, header.vertexCount)) {
				return;
			}
			scene_origin_[0] = header.originX;
			scene_origin_[1] = header.originY;
			scene_origin_[2] = header.originZ;
			static bool logged = false;
			if (!logged) {
				logged = true;
				log_info("minecraft's entities and particles: %u triangles in %u batches", header.vertexCount / 3, header.batchCount);
			}
		}

		void Things::on_avatar(const std::uint8_t* payload, std::uint32_t bytes)
		{
			avatar_.clear();  // no batches: first person
			if (bytes < sizeof(proto::RenAvatar)) {
				return;
			}
			proto::RenAvatar header;
			std::memcpy(&header, payload, sizeof(header));
			avatar_.read(payload, bytes, sizeof(header), header.batchCount, header.vertexCount);
		}

		void Things::clear()
		{
			scene_.clear();
			avatar_.clear();
			scene_list_.clear();
			avatar_list_.clear();
			scene_fresh_ = false;
			textures_.clear();
		}

		void Things::level_shutdown()
		{
			level_loaded_ = false;
			arrows_.clear();
			if (m_hRenderHandle != INVALID_CLIENT_RENDER_HANDLE) {
				ClientLeafSystem()->RemoveRenderable(m_hRenderHandle);
				m_hRenderHandle = INVALID_CLIENT_RENDER_HANDLE;
			}
		}

		void Things::shutdown()
		{
			level_shutdown();
			clear();
			entity_list_.clear();
			release_material(crack_material_);
			release_material(outline_material_);
			release_material(shadow_material_);
			shadow_texture_.release();
			crack_texture_ = nullptr;
		}

		void Things::ensure_materials(const AtlasView& atlas)
		{
			if (!outline_material_) {
				outline_material_ = make_unlit_material("outline", nullptr, Blend::OUTLINE);
			}
			if (!shadow_material_) {
				const auto image = shadow_image();
				shadow_texture_.set(image.data(), SHADOW_TEXTURE_SIZE, SHADOW_TEXTURE_SIZE);
				shadow_material_ = make_unlit_material("shadow", shadow_texture_.texture(), Blend::DECAL);
			}
			if (!crack_material_) {
				crack_material_ = make_unlit_material("cracks", atlas.texture, Blend::DECAL);
			} else if (crack_texture_ != atlas.texture) {
				set_material_texture(crack_material_, atlas.texture);
			}
			crack_texture_ = atlas.texture;
		}

		void Things::update()
		{
			// PreRender runs for every view (monitors, the intro's two scenes): once a frame is enough
			if (gpGlobals->framecount == built_frame_) {
				return;
			}
			built_frame_ = gpGlobals->framecount;
			auto&      s = client_session();
			const auto atlas = blocks_atlas();
			const bool active = level_loaded_ && !s.loading && s.have_mc && s.mc_in_world && atlas.texture && atlas.cutout;
			entity_list_.clear();
			selection_ = false;
			if (!active) {
				visible_ = false;
				return;
			}
			if (m_hRenderHandle == INVALID_CLIENT_RENDER_HANDLE) {
				ClientLeafSystem()->AddRenderable(this, RENDER_GROUP_TWOPASS);
			}
			ensure_materials(atlas);

			LightCache   light(light_scale());
			// the camera (last frame's), which shading turns triangles towards: in third person it's well
			// away from minecraft's eye
			const Vector& view = MainViewOrigin();
			const float   view_source[3] = { view.x, view.y, view.z };
			const McVec   view_mc = source_to_mc(view_source, s.slot);
			const double  eye[3] = { view_mc.x, view_mc.y, view_mc.z };
			build_entities(atlas, light, s.slot);
			if (std::chrono::steady_clock::now() - scene_time_ > SCENE_TIMEOUT) {
				scene_list_.clear();
			} else if (scene_fresh_) {
				scene_fresh_ = false;
				build_mesh(scene_list_, scene_, scene_origin_, atlas, light, s.slot, eye);
			}
			// the player's body where the camera follows it (minecraft's interpolated feet), only while
			// minecraft's camera is detached and drives the view
			avatar_list_.clear();
			if (third_person()) {
				build_mesh(avatar_list_, avatar_, s.pose.feet, atlas, light, s.slot, eye);
			}

			Vector lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			entity_list_.grow_bounds(lo, hi);
			scene_list_.grow_bounds(lo, hi);
			avatar_list_.grow_bounds(lo, hi);
			if (selection_) {
				lo = lo.Min(selection_lo_);
				hi = hi.Max(selection_hi_);
			}
			visible_ = lo.x <= hi.x;
			if (visible_) {
				set_bounds(lo, hi);
			}
		}

		void Things::build_entities(const AtlasView& atlas, LightCache& light, int slot)
		{
			auto& s = client_session();
			if (!s.link.read_world_entities(entities_)) {
				entities_.count = 0;
				entities_.hasSelection = 0;
			}
			for (std::uint32_t i = 0; i < entities_.count; ++i) {
				const auto& e = entities_.entities[i];
				if (e.kind == proto::kWeShadow) {
					emit_shadow(e, slot);
					continue;
				}
				Emitter out{ entity_list_, slot, { e.x, e.y, e.z }, atlas.u_scale, atlas.v_scale };
				float   centre[3];
				mc_to_source(e.x, e.y, e.z, slot, centre);
				if (e.kind == proto::kWeCrack) {
					// e.x/y/z is the box's minimum corner here
					const float mid[3] = { e.ext[0] * 0.5f, e.ext[1] * 0.5f, e.ext[2] * 0.5f };
					mc_to_source(e.x + mid[0], e.y + mid[1], e.z + mid[2], slot, centre);
				}
				const Vector here = light.at(centre);

				switch (e.kind) {
				case proto::kWeBlock: {
					// a dropped block: a small cube spinning about its centre
					const float size = e.scale;
					const float min[3] = { -size * 0.5f, -size * 0.5f, -size * 0.5f };
					const float extent[3] = { size, size, size };
					entity_list_.use(atlas.cutout, false);
					emit_box(out, min, extent, e.yaw * DEGREES, e.uv[0], e.uv[1], e.uv[2], e.tint, true, here);
					break;
				}
				case proto::kWeCrack: {
					const float min[3] = { 0.0f, 0.0f, 0.0f };
					entity_list_.use(crack_material_, true);
					emit_box(out, min, e.ext, 0.0f, e.uv[0], e.uv[0], e.uv[0], 0, false, here);
					break;
				}
				case proto::kWeArrow:
				case proto::kWeTrident: {
					// minecraft's arrows face (sin yaw, sin pitch, cos yaw)
					const float  yaw = e.yaw * DEGREES, pitch = e.pitch * DEGREES;
					const float  d[3] = { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
					std::uint8_t color[4];
					lit_white(here, 1.0f, 0, color);
					if (e.kind == proto::kWeArrow) {
						std::memcpy(arrow_uv_, e.uv, sizeof(arrow_uv_));  // for the arrows stuck in npcs
						have_arrow_uv_ = true;
					}
					entity_list_.use(atlas.cutout, false);
					emit_arrow(out, d, e.uv[0], e.uv[1], e.kind == proto::kWeTrident, color);
					break;
				}
				case proto::kWeItem: {
					// a flat sprite turning about the vertical
					const float  spin = e.yaw * DEGREES, half = e.scale * 0.5f;
					const float  rx = std::cos(spin) * half, rz = std::sin(spin) * half;
					const float  p[4][3] = { { -rx, half, -rz }, { rx, half, rz }, { rx, -half, rz }, { -rx, -half, -rz } };
					std::uint8_t color[4];
					lit_white(here, 1.0f, 0, color);
					entity_list_.use(atlas.cutout, false);
					out.quad(p, e.uv[0], color);
					break;
				}
				default:
					break;
				}
			}

			if (have_arrow_uv_) {
				arrows_.emit(entity_list_, atlas.cutout, light, slot, arrow_uv_[0], arrow_uv_[1], atlas.u_scale, atlas.v_scale);
			}

			selection_ = entities_.hasSelection != 0;
			if (selection_) {
				float lo[3], hi[3];
				mc_to_source(entities_.selMin[0] - OUTLINE_GROW, entities_.selMin[1] - OUTLINE_GROW, entities_.selMin[2] - OUTLINE_GROW, slot, lo);
				mc_to_source(entities_.selMax[0] + OUTLINE_GROW, entities_.selMax[1] + OUTLINE_GROW, entities_.selMax[2] + OUTLINE_GROW, slot, hi);
				// minecraft's z flips into source's y: sort each axis again
				for (int k = 0; k < 3; ++k) {
					selection_lo_[k] = std::min(lo[k], hi[k]);
					selection_hi_[k] = std::max(lo[k], hi[k]);
				}
			}
		}

		void Things::emit_shadow(const proto::WorldEntity& e, int slot)
		{
			// the player's own only while its body shows
			const auto& mc = client_session().mc;
			if (!third_person() && std::fabs(e.x - mc.x) < 0.3 && std::fabs(e.z - mc.z) < 0.3 && std::fabs(e.y - mc.y) < 0.5) {
				return;
			}
			const double ground = ground_below(e.x, e.y, e.z, slot);
			if (std::isnan(ground)) {
				return;
			}
			const float fade = 1.0f - static_cast<float>(std::clamp((e.y - ground) / SHADOW_REACH, 0.0, 1.0));
			if (fade <= 0.0f) {
				return;
			}
			const float        radius = std::max(0.3f, e.scale * 0.75f);
			const float        lift = static_cast<float>(ground - e.y) + SHADOW_LIFT;
			const float        p[4][3] = { { -radius, lift, -radius }, { radius, lift, -radius }, { radius, lift, radius }, { -radius, lift, radius } };
			const float        uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			const std::uint8_t color[4] = { 0, 0, 0, static_cast<std::uint8_t>(255.0f * SHADOW_STRENGTH * fade) };
			Emitter            out{ entity_list_, slot, { e.x, e.y, e.z }, 1.0f, 1.0f };
			entity_list_.use(shadow_material_, true);
			out.quad(p, uv, color);
		}

		void Things::build_mesh(DrawList& list, const McMesh& mesh, const double origin[3], const AtlasView& atlas, LightCache& light, int slot,
			const double eye_mc[3])
		{
			list.clear();
			const float eye[3] = { static_cast<float>(eye_mc[0] - origin[0]), static_cast<float>(eye_mc[1] - origin[1]),
				static_cast<float>(eye_mc[2] - origin[2]) };
			Emitter out{ list, slot, { origin[0], origin[1], origin[2] }, 1.0f, 1.0f };
			for (const auto& batch : mesh.batches) {
				IMaterial* cutout = atlas.cutout;
				IMaterial* translucent = atlas.translucent;
				float      u_scale = atlas.u_scale, v_scale = atlas.v_scale;
				if (batch.texture != 0) {
					const auto it = textures_.find(batch.texture);
					if (it == textures_.end() || !it->second->cutout) {
						continue;  // its texture hasn't come yet
					}
					cutout = it->second->cutout;
					translucent = it->second->translucent;
					u_scale = it->second->texture.u_scale();
					v_scale = it->second->texture.v_scale();
				}
				out.u_scale = u_scale;
				out.v_scale = v_scale;
				const bool blended = (batch.flags & 1) != 0;
				for (std::uint32_t i = batch.first; i + 2 < batch.first + batch.count; i += 3) {
					const proto::RenVertex* tri = &mesh.vertices[i];
					const float             p[3][3] = { { tri[0].x, tri[0].y, tri[0].z }, { tri[1].x, tri[1].y, tri[1].z }, { tri[2].x, tri[2].y, tri[2].z } };

					// shading: minecraft's face brightness, or its entity lighting by the triangle's
					// normal turned towards the camera, or none (particles)
					const int face = static_cast<int>((tri[0].flags >> 4) & 7);
					float     shade = 1.0f;
					if (face >= 1 && face <= 6) {
						shade = FACE_SHADE[face];
					} else if (face == 7) {
						const float e1[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] };
						const float e2[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };
						float       n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
						const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
						if (length > 1e-8f) {
							const float towards = n[0] * (eye[0] - p[0][0]) + n[1] * (eye[1] - p[0][1]) + n[2] * (eye[2] - p[0][2]);
							const float sign = towards < 0.0f ? -1.0f : 1.0f;
							for (auto& c : n) {
								c *= sign / length;
							}
							shade = entity_shade(n);
						}
					}

					float centre[3];
					mc_to_source(origin[0] + (p[0][0] + p[1][0] + p[2][0]) / 3.0, origin[1] + (p[0][1] + p[1][1] + p[2][1]) / 3.0,
						origin[2] + (p[0][2] + p[1][2] + p[2][2]) / 3.0, slot, centre);
					const Vector here = light.at(centre);

					if (tri[0].flags & 4) {
						list.use(outline_material_, true);  // untextured: vertex colour only
					} else {
						const bool see_through = blended || (tri[0].flags & 2) != 0;
						list.use(see_through ? translucent : cutout, see_through);
					}
					for (int k = 0; k < 3; ++k) {
						std::uint8_t color[4];
						lit_color(reinterpret_cast<const std::uint8_t*>(&tri[k].color), shade, here, static_cast<int>(tri[k].light & 0xFF), color);
						out.vertex(p[k], tri[k].u, tri[k].v, color);
					}
				}
			}
		}

		void Things::set_bounds(const Vector& lo, const Vector& hi)
		{
			const Vector origin = (lo + hi) * 0.5f;
			const Vector mins = lo - origin, maxs = hi - origin;
			if (origin == origin_ && mins == mins_ && maxs == maxs_) {
				return;
			}
			origin_ = origin;
			mins_ = mins;
			maxs_ = maxs;
			PositionMatrix(origin_, transform_);
			if (m_hRenderHandle != INVALID_CLIENT_RENDER_HANDLE) {
				ClientLeafSystem()->RenderableChanged(m_hRenderHandle);
			}
		}

		void Things::draw_outline()
		{
			// each edge a thin strip facing the camera, about two pixels wide at any distance
			const Vector& eye = CurrentViewOrigin();
			auto          corner = [&](int i) {
                return Vector((i & 1) ? selection_hi_.x : selection_lo_.x, (i & 2) ? selection_hi_.y : selection_lo_.y, (i & 4) ? selection_hi_.z : selection_lo_.z);
			};
			static constexpr int EDGES[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
			DrawVertex           strips[12 * 6];
			int                  count = 0;
			for (const auto& edge : EDGES) {
				const Vector a = corner(edge[0]), b = corner(edge[1]);
				const Vector mid = (a + b) * 0.5f;
				Vector       across = CrossProduct(b - a, eye - mid);
				if (across.NormalizeInPlace() < 1e-4f) {
					continue;  // looking straight down the edge
				}
				across *= (eye - mid).Length() * OUTLINE_WIDTH * 0.5f;
				const Vector quad[4] = { a - across, b - across, b + across, a + across };
				for (int k : { 0, 1, 2, 0, 2, 3 }) {
					DrawVertex& v = strips[count++];
					v.position[0] = quad[k].x, v.position[1] = quad[k].y, v.position[2] = quad[k].z;
					std::memcpy(v.color, OUTLINE_COLOR, 4);
					v.uv[0] = v.uv[1] = 0.0f;
				}
			}
			draw_triangles(outline_material_, strips, count);
		}

		int Things::DrawModel(int flags)
		{
			if (!visible_) {
				return 0;
			}
			const bool           translucent_pass = (flags & STUDIO_TRANSPARENCY) != 0;
			CMatRenderContextPtr context(materials);
			context->MatrixMode(MATERIAL_MODEL);
			context->PushMatrix();
			context->LoadIdentity();  // the vertices are already in world space
			entity_list_.draw(translucent_pass);
			scene_list_.draw(translucent_pass);
			avatar_list_.draw(translucent_pass);
			if (translucent_pass && selection_ && outline_material_) {
				draw_outline();
			}
			context->MatrixMode(MATERIAL_MODEL);
			context->PopMatrix();
			return 1;
		}

		/// keeps the renderable in step with source's levels and builds its geometry before each frame.
		class HalfCraftThingsSystem final : public CAutoGameSystemPerFrame
		{
		public:
			HalfCraftThingsSystem() : CAutoGameSystemPerFrame("HalfCraftThings") {}

			void LevelInitPostEntity() override { things().level_init(); }
			void LevelShutdownPreEntity() override { things().level_shutdown(); }
			void Shutdown() override { things().shutdown(); }
			void PreRender() override { things().update(); }
		};

		HalfCraftThingsSystem g_things_system;
	}

	void things_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes)
	{
		switch (type) {
		case proto::kRenTexture:
			things().on_texture(payload, bytes);
			return;
		case proto::kRenScene:
			things().on_scene(payload, bytes);
			return;
		case proto::kRenAvatar:
			things().on_avatar(payload, bytes);
			return;
		case proto::kRenClearAll:
			things().clear();
			return;
		default:
			return;
		}
	}
}

// server.dll hands minecraft's arrows that stuck in npcs over (hc_bridge.h)
extern "C" __declspec(dllexport) void HalfCraft_StickArrow(int entindex, const float hit[3], const float direction[3])
{
	halfcraft::things().stuck_arrows().queue(entindex, Vector(hit[0], hit[1], hit[2]), Vector(direction[0], direction[1], direction[2]));
}
