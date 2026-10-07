// client.dll: half-life's side of minecraft's debug screen (see hc_debug.h).

#include "cbase.h"
#include "c_baseplayer.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"
#include "tier3/tier3.h"
#include "vgui/ILocalize.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cctype>
#include <deque>
#include <string>
#include <vector>

#include "client/hc_block_lights.h"
#include "client/hc_client.h"
#include "client/hc_debug.h"
#include "core/hc_chapters.h"
#include "core/hc_log.h"
#include "core/hc_text.h"
#include "core/hc_units.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr float  PUBLISH_SECONDS = 0.1f;  // minecraft's debug screen needs no more than this
		constexpr double FPS_WINDOW_SECONDS = 1.0;
		constexpr char   CHAPTER_CFGS[] = "cfg/chapter*.cfg";

		/// the chapters of the game folder (its cfg/chapter*.cfg) and their titles, and the current map's.
		class Chapters
		{
		public:
			/// the map's chapter ("9a") and its title in the player's language, worked out once per map.
			void lookup(const std::string& map, std::string& chapter, std::string& title)
			{
				if (map != map_) {
					map_ = map;
					find(map);
				}
				chapter = chapter_;
				title = title_;
			}

		private:
			void load()
			{
				loaded_ = true;
				const std::string folder = engine->GetGameDirectory();
				folder_ = folder.substr(folder.find_last_of("/\\") + 1);
				FileFindHandle_t handle = FILESYSTEM_INVALID_FIND_HANDLE;
				for (const char* file = filesystem->FindFirstEx(CHAPTER_CFGS, "MOD", &handle); file; file = filesystem->FindNext(handle)) {
					const std::string name = file;
					constexpr std::size_t PREFIX = sizeof("chapter") - 1;
					constexpr std::size_t SUFFIX = sizeof(".cfg") - 1;
					if (name.size() <= PREFIX + SUFFIX) {
						continue;
					}
					CUtlBuffer buffer(0, 0, CUtlBuffer::TEXT_BUFFER);
					if (!filesystem->ReadFile(("cfg/" + name).c_str(), "MOD", buffer)) {
						continue;
					}
					const std::string text(static_cast<const char*>(buffer.Base()), buffer.TellPut());
					const std::string map = chapter_cfg_map(text);
					if (!map.empty()) {
						std::string chapter = name.substr(PREFIX, name.size() - PREFIX - SUFFIX);
						for (auto& ch : chapter) {
							ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
						}
						starts_.push_back({ chapter, map });
					}
				}
				filesystem->FindClose(handle);
				log_info("debug screen: %d chapters in %s's %s", static_cast<int>(starts_.size()), folder_.c_str(), CHAPTER_CFGS);
			}

			void find(const std::string& map)
			{
				if (!loaded_) {
					load();
				}
				chapter_ = map.empty() ? std::string() : chapter_of(starts_, map);
				title_.clear();
				if (chapter_.empty()) {
					if (!map.empty()) {
						log_info("debug screen: %s is in no chapter", map.c_str());
					}
					return;
				}
				// the chapter dialog's own strings: "<game folder>_Chapter<N>_Title" (tools/engines.ps1)
				const std::string token = "#" + folder_ + "_Chapter" + chapter_ + "_Title";
				if (const wchar_t* wide = g_pVGuiLocalize ? g_pVGuiLocalize->Find(token.c_str()) : nullptr) {
					char utf8[256];
					g_pVGuiLocalize->ConvertUnicodeToANSI(wide, utf8, sizeof(utf8));
					title_ = utf8;
				}
				log_info("debug screen: %s is chapter %s (%s)", map.c_str(), chapter_.c_str(), title_.empty() ? token.c_str() : title_.c_str());
			}

			bool                      loaded_ = false;
			std::string               folder_;
			std::vector<ChapterStart> starts_;
			std::string               map_;
			std::string               chapter_;
			std::string               title_;
		};

		/// half-life's frame rate over the last second.
		class FrameClock
		{
		public:
			void frame(float seconds)
			{
				frames_.push_back(seconds);
				total_ += seconds;
				while (frames_.size() > 1 && total_ - frames_.front() >= FPS_WINDOW_SECONDS) {
					total_ -= frames_.front();
					frames_.pop_front();
				}
			}

			[[nodiscard]] int   frames() const { return static_cast<int>(frames_.size()); }
			[[nodiscard]] float fps() const { return total_ > 0.0 ? static_cast<float>(frames_.size() / total_) : 0.0f; }
			[[nodiscard]] float worst_ms() const { return frames_.empty() ? 0.0f : *std::max_element(frames_.begin(), frames_.end()) * 1000.0f; }

		private:
			std::deque<float> frames_;
			double            total_ = 0.0;
		};

		class HalfCraftDebugSystem final : public CAutoGameSystemPerFrame
		{
		public:
			HalfCraftDebugSystem() : CAutoGameSystemPerFrame("HalfCraftDebug") {}

			void Update(float) override
			{
				const float frame = gpGlobals->absoluteframetime;
				clock_.frame(frame);
				publish_in_ -= frame;
				auto& s = client_session();
				if (publish_in_ > 0.0f || !s.link_ready || !s.link.mc_alive()) {
					return;
				}
				publish_in_ = PUBLISH_SECONDS;
				publish(s);
			}

		private:
			void publish(ClientSession& s)
			{
				proto::HostDebug debug{};
				debug.flags = (s.puppeting ? proto::kDebugPuppet : 0u) | (s.minecraft_owns_input ? proto::kDebugMinecraftInput : 0u) |
							  (s.minecraft_hud ? proto::kDebugMinecraftHud : 0u);
				C_BasePlayer* player = C_BasePlayer::GetLocalPlayer();
				const bool    in_game = player && engine->IsInGame() && !engine->IsLevelMainMenuBackground();
				std::string   map, chapter, title;
				if (in_game) {
					map = map_base_name(engine->GetLevelName());
					const Vector& origin = player->GetAbsOrigin();
					const QAngle& angles = player->EyeAngles();
					for (int k = 0; k < 3; ++k) {
						debug.origin[k] = origin[k];
						debug.angles[k] = angles[k];
					}
				}
				chapters_.lookup(map, chapter, title);
				copy_utf8(debug.map, map);
				copy_utf8(debug.chapter, chapter);
				copy_utf8(debug.chapterTitle, title);
				debug.fps = clock_.fps();
				debug.worstFrameMs = clock_.worst_ms();
				debug.overlayMs = overlay_cost_ms(clock_.frames());
				debug.slot = s.slot.index;
				debug.gridZ = s.slot.grid_z;
				debug.collisionEpoch = s.epoch;
				const auto backlog = s.link.backlog();
				debug.inputPending = static_cast<std::uint32_t>(backlog.input);
				debug.eventPending = static_cast<std::uint32_t>(backlog.events);
				debug.collisionPending = backlog.collision_bytes;
				debug.renderPending = backlog.render_bytes;
				const auto lights = block_lights_stats();
				debug.lightEmitters = lights.emitters;
				debug.lights = lights.lights;
				debug.shadowedLights = lights.shadowed;
				s.link.write_host_debug(debug);
			}

			FrameClock clock_;
			Chapters   chapters_;
			float      publish_in_ = 0.0f;
		};

		HalfCraftDebugSystem g_debug_system;
	}
}
