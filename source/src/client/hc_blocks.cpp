// client.dll: minecraft's blocks drawn in source's world (see hc_blocks.h).

#include "cbase.h"
#include "c_baseplayer.h"
#include "clientleafsystem.h"
#include "model_types.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

#include "client/hc_blocks.h"
#include "client/hc_light.h"
#include "client/hc_mesh.h"
#include "client/hc_texture.h"
#include "core/hc_link.h"
#include "core/hc_log.h"
#include "core/hc_units.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr int   SECTION_BLOCKS = 16;
		constexpr int   MESH_MAX_VERTICES = 30000;  // per static mesh (a multiple of 3)
		constexpr int   BUILDS_PER_FRAME = 8;
		constexpr auto  BUILD_BUDGET = std::chrono::microseconds(4000);
		constexpr float LIGHT_PROBE_BLOCKS = 0.3f;  // light is sampled this far out in front of a face
		constexpr float KEEP_SLOT_BLOCKS = 600.0f;  // sections further than this from the map's slot are dropped

		// ---- one section ----------------------------------------------------------------------
		class Section final : public CDefaultClientRenderable
		{
		public:
			Section(int sx, int sy, int sz) : sx_(sx), sy_(sy), sz_(sz) { SetIdentityMatrix(transform_); }
			~Section() override { unload(); }

			void set_vertices(const proto::RenVertex* vertices, std::uint32_t count)
			{
				vertices_.assign(vertices, vertices + count);
				dirty_ = true;
			}

			[[nodiscard]] bool dirty() const { return dirty_; }
			void               mark_dirty() { dirty_ = true; }
			[[nodiscard]] int  mc_x() const { return sx_ * SECTION_BLOCKS; }

			/// meshes from the vertices; registers with the leaf system. needs a loaded map.
			/// @param light_scale - how much brighter than the raw light sample (source's overbright)
			void build(const McTexture& atlas, IMaterial* cutout, IMaterial* translucent, int slot, float light_scale);
			/// drops meshes and the leaf system registration (vertices are kept).
			void unload();

			// IClientRenderable
			const Vector&      GetRenderOrigin() override { return origin_; }
			const QAngle&      GetRenderAngles() override { return vec3_angle; }
			const matrix3x4_t& RenderableToWorldTransform() override { return transform_; }
			bool               ShouldDraw() override { return !opaque_.empty() || !translucent_.empty(); }
			bool               IsTransparent() override { return !translucent_.empty(); }
			bool               IsTwoPass() override { return !translucent_.empty() && !opaque_.empty(); }
			void               GetRenderBounds(Vector& mins, Vector& maxs) override
			{
				mins = mins_;
				maxs = maxs_;
			}
			int DrawModel(int flags) override;

		private:
			int                      sx_, sy_, sz_;
			std::vector<proto::RenVertex> vertices_;
			bool                     dirty_ = true;
			std::vector<IMesh*>      opaque_, translucent_;
			IMaterial*               cutout_material_ = nullptr;
			IMaterial*               translucent_material_ = nullptr;
			Vector                   origin_{ 0.0f, 0.0f, 0.0f };
			Vector                   mins_{ 0.0f, 0.0f, 0.0f }, maxs_{ 0.0f, 0.0f, 0.0f };
			matrix3x4_t              transform_;
		};

		void Section::build(const McTexture& atlas, IMaterial* cutout, IMaterial* translucent, int slot, float light_scale)
		{
			unload();
			dirty_ = false;
			cutout_material_ = cutout;
			translucent_material_ = translucent;
			if (vertices_.empty()) {
				return;
			}

			// half-life's light where the faces are, shared by the corners that meet there
			LightCache light(light_scale);
			const double ox = sx_ * SECTION_BLOCKS, oy = sy_ * SECTION_BLOCKS, oz = sz_ * SECTION_BLOCKS;
			const float  u_scale = atlas.u_scale(), v_scale = atlas.v_scale();
			std::vector<DrawVertex> solid, see_through;
			solid.reserve(vertices_.size());
			Vector lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (const auto& v : vertices_) {
				int face = static_cast<int>((v.flags >> 4) & 7);
				face = face <= 6 ? face : 0;
				const double x = ox + v.x, y = oy + v.y, z = oz + v.z;
				const float* n = FACE_NORMAL[face];
				float probe[3];
				mc_to_source(x + n[0] * LIGHT_PROBE_BLOCKS, y + n[1] * LIGHT_PROBE_BLOCKS, z + n[2] * LIGHT_PROBE_BLOCKS, slot, probe);

				DrawVertex out;
				mc_to_source(x, y, z, slot, out.position);
				lit_color(reinterpret_cast<const std::uint8_t*>(&v.color), FACE_SHADE[face], light.at(probe), static_cast<int>(v.light & 0xF), out.color);
				out.uv[0] = v.u * u_scale;
				out.uv[1] = v.v * v_scale;
				for (int k = 0; k < 3; ++k) {
					lo[k] = std::min(lo[k], out.position[k]);
					hi[k] = std::max(hi[k], out.position[k]);
				}
				(v.flags & 2 ? see_through : solid).push_back(out);
			}

			auto make_meshes = [&](const std::vector<DrawVertex>& list, IMaterial* material, std::vector<IMesh*>& meshes) {
				for (std::size_t first = 0; first + 2 < list.size(); first += MESH_MAX_VERTICES) {
					const int count = static_cast<int>(std::min<std::size_t>(MESH_MAX_VERTICES, list.size() - first));
					if (IMesh* mesh = make_static_mesh(material, list.data() + first, count)) {
						meshes.push_back(mesh);
					}
				}
			};
			make_meshes(solid, cutout, opaque_);
			make_meshes(see_through, translucent, translucent_);
			if (opaque_.empty() && translucent_.empty()) {
				return;
			}

			origin_ = (lo + hi) * 0.5f;
			mins_ = lo - origin_;
			maxs_ = hi - origin_;
			PositionMatrix(origin_, transform_);
			const RenderGroup_t group = IsTwoPass() ? RENDER_GROUP_TWOPASS : (IsTransparent() ? RENDER_GROUP_TRANSLUCENT_ENTITY : RENDER_GROUP_OPAQUE_ENTITY);
			ClientLeafSystem()->AddRenderable(this, group);
		}

		void Section::unload()
		{
			if (m_hRenderHandle != INVALID_CLIENT_RENDER_HANDLE) {
				ClientLeafSystem()->RemoveRenderable(m_hRenderHandle);
				m_hRenderHandle = INVALID_CLIENT_RENDER_HANDLE;
			}
			for (IMesh* mesh : opaque_) {
				destroy_static_mesh(mesh);
			}
			for (IMesh* mesh : translucent_) {
				destroy_static_mesh(mesh);
			}
			opaque_.clear();
			translucent_.clear();
			dirty_ = true;
		}

		int Section::DrawModel(int flags)
		{
			const bool translucent_pass = (flags & STUDIO_TRANSPARENCY) != 0;
			const auto& meshes = translucent_pass ? translucent_ : opaque_;
			if (meshes.empty()) {
				return 0;
			}
			CMatRenderContextPtr context(materials);
			context->MatrixMode(MATERIAL_MODEL);
			context->PushMatrix();
			context->LoadIdentity();  // the vertices are already in world space
			context->Bind(translucent_pass ? translucent_material_ : cutout_material_);
			for (IMesh* mesh : meshes) {
				mesh->Draw();
			}
			context->MatrixMode(MATERIAL_MODEL);
			context->PopMatrix();
			return 1;
		}

		// ---- all of it ------------------------------------------------------------------------
		using SectionKey = std::tuple<int, int, int>;

		struct Blocks
		{
			McTexture                                      atlas{ "atlas" };
			IMaterial*                                     cutout = nullptr;
			IMaterial*                                     translucent = nullptr;
			std::map<SectionKey, std::unique_ptr<Section>> sections;
			int                                            slot = 0;
			bool                                           level_loaded = false;
			float                                          light_scale = 0.0f;  // as last built

			// solid blocks for server.dll (hc_bridge.h): every section ever reported, with the
			// version it last changed at (emptied ones stay, so the server hears they're gone)
			std::mutex                                                   solids_lock;
			std::map<SectionKey, std::pair<SolidSection, std::uint32_t>> solids;
			std::uint32_t                                                solids_version = 0;
		};

		Blocks& blocks()
		{
			static Blocks instance;
			return instance;
		}

		void point_materials_at_atlas(Blocks& b)
		{
			ITexture* texture = b.atlas.texture();
			if (!b.cutout) {
				b.cutout = make_unlit_material("blocks_cutout", texture, Blend::CUTOUT);
				b.translucent = make_unlit_material("blocks_translucent", texture, Blend::TRANSLUCENT);
				return;
			}
			set_material_texture(b.cutout, texture);
			set_material_texture(b.translucent, texture);
		}

		bool in_level()
		{
			return engine->IsInGame() && !engine->IsLevelMainMenuBackground() && C_BasePlayer::GetLocalPlayer() != nullptr;
		}

		/// keeps the sections in step with source's levels: their meshes and leaf-system
		/// registrations don't survive a level change, their vertices do (minecraft won't send them
		/// again unless they change).
		class HalfCraftBlocksSystem final : public CAutoGameSystem
		{
		public:
			HalfCraftBlocksSystem() : CAutoGameSystem("HalfCraftBlocks") {}

			void LevelInitPostEntity() override { blocks().level_loaded = true; }

			void LevelShutdownPreEntity() override
			{
				auto& b = blocks();
				b.level_loaded = false;
				for (auto& entry : b.sections) {
					entry.second->unload();
				}
			}
		};

		HalfCraftBlocksSystem g_blocks_system;

		/// one section's solids changed (count 0 / bits null: none left).
		void set_solids(std::int32_t sx, std::int32_t sy, std::int32_t sz, std::uint32_t count, const std::uint8_t* bits)
		{
			auto&                       b = blocks();
			std::lock_guard<std::mutex> lock(b.solids_lock);
			auto& entry = b.solids[SectionKey{ sx, sy, sz }];
			if (!bits && entry.first.count == 0 && entry.second != 0) {
				return;  // empty already
			}
			entry.first.sx = sx;
			entry.first.sy = sy;
			entry.first.sz = sz;
			entry.first.count = bits ? count : 0;
			if (bits) {
				std::memcpy(entry.first.bits, bits, sizeof(entry.first.bits));
			} else {
				std::memset(entry.first.bits, 0, sizeof(entry.first.bits));
			}
			entry.second = ++b.solids_version;
		}

		void clear_solids(const std::vector<SectionKey>& keys)
		{
			for (const auto& key : keys) {
				set_solids(std::get<0>(key), std::get<1>(key), std::get<2>(key), 0, nullptr);
			}
		}
	}

	void blocks_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes)
	{
		auto& b = blocks();
		switch (type) {
		case proto::kRenAtlas: {
			if (bytes < sizeof(proto::RenAtlas)) {
				return;
			}
			proto::RenAtlas header;
			std::memcpy(&header, payload, sizeof(header));
			const std::uint64_t pixels = std::uint64_t(header.width) * header.height * 4;
			if (header.width == 0 || header.height == 0 || header.width > 16384 || header.height > 16384 || sizeof(header) + pixels > bytes) {
				log_warning("bad block atlas (%ux%u, %u bytes)", header.width, header.height, bytes);
				return;
			}
			const bool resized = b.atlas.set(payload + sizeof(header), static_cast<int>(header.width), static_cast<int>(header.height));
			point_materials_at_atlas(b);
			if (resized) {
				for (auto& entry : b.sections) {
					entry.second->mark_dirty();  // their uvs scale with the atlas
				}
			}
			log_info("block atlas %ux%u", header.width, header.height);
			return;
		}
		case proto::kRenAtlasRegion: {
			if (bytes < sizeof(proto::RenAtlasRegion)) {
				return;
			}
			proto::RenAtlasRegion region;
			std::memcpy(&region, payload, sizeof(region));
			if (sizeof(region) + std::uint64_t(region.width) * region.height * 4 <= bytes) {
				b.atlas.update_region(static_cast<int>(region.x), static_cast<int>(region.y), static_cast<int>(region.width), static_cast<int>(region.height),
					payload + sizeof(region));
			}
			return;
		}
		case proto::kRenSection: {
			if (bytes < sizeof(proto::RenSection)) {
				return;
			}
			proto::RenSection header;
			std::memcpy(&header, payload, sizeof(header));
			if (sizeof(header) + std::uint64_t(header.vertexCount) * sizeof(proto::RenVertex) > bytes) {
				return;
			}
			const SectionKey key{ header.sx, header.sy, header.sz };
			if (header.vertexCount == 0) {
				b.sections.erase(key);
				return;
			}
			auto& section = b.sections[key];
			if (!section) {
				section = std::make_unique<Section>(header.sx, header.sy, header.sz);
			}
			section->set_vertices(reinterpret_cast<const proto::RenVertex*>(payload + sizeof(header)), header.vertexCount);
			return;
		}
		case proto::kRenSolids: {
			if (bytes < sizeof(proto::RenSolids)) {
				return;
			}
			proto::RenSolids header;
			std::memcpy(&header, payload, sizeof(header));
			const bool has_bits = header.count > 0 && sizeof(header) + 512 <= bytes;
			set_solids(header.sx, header.sy, header.sz, has_bits ? header.count : 0, has_bits ? payload + sizeof(header) : nullptr);
			return;
		}
		case proto::kRenClearAll:
			blocks_clear();
			return;
		default:
			return;  // entities, the player's body, lights: not drawn yet
		}
	}

	void blocks_update()
	{
		auto& b = blocks();
		if (!b.level_loaded || !in_level() || !b.atlas.texture() || !b.cutout || !b.cutout->IsPrecached() || !b.translucent->IsPrecached()) {
			return;
		}
		const float light_scale = halfcraft::light_scale();
		if (light_scale != b.light_scale) {
			b.light_scale = light_scale;
			for (auto& entry : b.sections) {
				entry.second->mark_dirty();
			}
		}
		const auto start = std::chrono::steady_clock::now();
		int        built = 0;
		for (auto& entry : b.sections) {
			Section& section = *entry.second;
			if (!section.dirty()) {
				continue;
			}
			section.build(b.atlas, b.cutout, b.translucent, b.slot, light_scale);
			if (++built >= BUILDS_PER_FRAME || std::chrono::steady_clock::now() - start > BUILD_BUDGET) {
				break;
			}
		}
	}

	void blocks_set_slot(int slot)
	{
		auto& b = blocks();
		if (slot == b.slot) {
			return;
		}
		b.slot = slot;
		// another map: what minecraft has around the old one isn't anywhere near this one
		const float centre = static_cast<float>(slot * MAP_SLOT_BLOCKS);
		for (auto it = b.sections.begin(); it != b.sections.end();) {
			if (std::fabs(static_cast<float>(it->second->mc_x()) - centre) > KEEP_SLOT_BLOCKS) {
				it = b.sections.erase(it);
			} else {
				it->second->mark_dirty();
				++it;
			}
		}
		std::vector<SectionKey> far;
		{
			std::lock_guard<std::mutex> lock(b.solids_lock);
			for (const auto& entry : b.solids) {
				if (entry.second.first.count > 0 && std::fabs(static_cast<float>(std::get<0>(entry.first) * SECTION_BLOCKS) - centre) > KEEP_SLOT_BLOCKS) {
					far.push_back(entry.first);
				}
			}
		}
		clear_solids(far);
	}

	void blocks_clear()
	{
		auto& b = blocks();
		b.sections.clear();
		std::vector<SectionKey> all;
		{
			std::lock_guard<std::mutex> lock(b.solids_lock);
			for (const auto& entry : b.solids) {
				if (entry.second.first.count > 0) {
					all.push_back(entry.first);
				}
			}
		}
		clear_solids(all);
	}

	void blocks_shutdown()
	{
		auto& b = blocks();
		b.sections.clear();
		release_material(b.cutout);
		release_material(b.translucent);
		b.atlas.release();
	}

	bool blocks_solid(int x, int y, int z)
	{
		auto floor_div = [](int v) { return v >= 0 ? v / SECTION_BLOCKS : (v - SECTION_BLOCKS + 1) / SECTION_BLOCKS; };
		auto&                       b = blocks();
		std::lock_guard<std::mutex> lock(b.solids_lock);
		const auto it = b.solids.find(SectionKey{ floor_div(x), floor_div(y), floor_div(z) });
		if (it == b.solids.end() || it->second.first.count == 0) {
			return false;
		}
		const int bit = (x & 15) + 16 * (z & 15) + 256 * (y & 15);
		return (it->second.first.bits[bit >> 3] >> (bit & 7)) & 1;
	}

	AtlasView blocks_atlas()
	{
		const auto& b = blocks();
		return { b.atlas.texture(), b.cutout, b.translucent, b.atlas.u_scale(), b.atlas.v_scale() };
	}

	int solids_since(std::uint32_t since, SolidSection* out, int max, std::uint32_t* now)
	{
		auto&                       b = blocks();
		std::lock_guard<std::mutex> lock(b.solids_lock);
		int                         changed = 0;
		for (const auto& entry : b.solids) {
			if (entry.second.second > since) {
				if (out && changed < max) {
					out[changed] = entry.second.first;
				}
				++changed;
			}
		}
		if (now) {
			*now = b.solids_version;
		}
		return changed;
	}
}

// server.dll's way to minecraft's solid blocks (hc_bridge.h)
extern "C" __declspec(dllexport) int HalfCraft_SolidsSince(std::uint32_t since, halfcraft::SolidSection* out, int max, std::uint32_t* now)
{
	return halfcraft::solids_since(since, out, max, now);
}
