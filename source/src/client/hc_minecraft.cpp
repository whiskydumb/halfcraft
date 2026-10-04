// client.dll: keeps a release's bundled minecraft running beside half-life (core/hc_prism.h) and
// says on screen what it's doing until it has connected. HalfCraft.exe starts it before half-life;
// this starts it when half-life was started some other way, and again when it quit on its own (a
// crash). a development checkout has no bundle: the gradle dev client is started by hand there.

#include "cbase.h"
#include "ienginevgui.h"

#include <vgui/ISurface.h>
#include <vgui_controls/Panel.h>

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <chrono>
#include <cwchar>
#include <string>

#include "client/hc_client.h"
#include "core/hc_log.h"
#include "core/hc_prism.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr std::chrono::milliseconds CHECK_INTERVAL{ 1000 };
		constexpr int                       MAX_STARTS = 3;  // per half-life run: one that keeps dying isn't started forever

		class MinecraftKeeper
		{
		public:
			void init(const char* game_dir)
			{
				bundle_ = prism::find_bundle(game_dir);
				if (bundle_.empty()) {
					log_info("no bundled minecraft beside %s (a development checkout): start minecraft yourself", game_dir);
				} else {
					log_info("bundled minecraft: %s", bundle_.u8string().c_str());
				}
			}

			void update(bool connected)
			{
				const auto now = std::chrono::steady_clock::now();
				if (bundle_.empty() || now < next_check_) {
					return;
				}
				next_check_ = now + CHECK_INTERVAL;
				if (connected) {
					state_ = State::kConnected;
					came_up_ = true;
					return;
				}
				if (prism::minecraft_running()) {
					state_ = State::kLoading;  // or the last half-life's, on its way out
					came_up_ = true;
					return;
				}
				if (prism::launcher_running(bundle_)) {
					state_ = prism::signed_in(bundle_) ? State::kStarting : State::kSigningIn;
					return;
				}
				// nothing running. start it, unless the last start never got as far as minecraft (no
				// account, a failed download: starting prism again would just ask again)
				if ((starts_ > 0 && !came_up_) || starts_ >= MAX_STARTS) {
					if (state_ != State::kStopped && state_ != State::kFailed) {
						log_warning("minecraft isn't running; not starting it again (%d starts)", starts_);
						state_ = State::kStopped;
					}
					return;
				}
				log_info(came_up_ ? "minecraft has quit; starting it again" : "starting the bundled minecraft");
				came_up_ = false;
				++starts_;
				std::wstring error;
				if (prism::start(bundle_, error)) {
					state_ = State::kStarting;
				} else {
					log_warning("%ls", error.c_str());
					failure_ = L"Couldn't start Minecraft: " + error;
					state_ = State::kFailed;
				}
			}

			/// what the player should know, or nullptr while there's nothing to say.
			const wchar_t* status() const
			{
				switch (state_) {
				case State::kStarting:
					return L"Minecraft is starting…";
				case State::kSigningIn:
					return L"Sign in to Minecraft in the Prism Launcher window (Alt+Tab)";
				case State::kLoading:
					return L"Minecraft is loading…";
				case State::kStopped:
					return L"Minecraft isn't running: quit and start HalfCraft again";
				case State::kFailed:
					return failure_.c_str();
				default:
					return nullptr;
				}
			}

		private:
			enum class State
			{
				kIdle,  // not checked yet, or no bundle
				kStarting,
				kSigningIn,
				kLoading,
				kConnected,
				kStopped,
				kFailed,
			};

			std::filesystem::path                 bundle_;
			State                                 state_ = State::kIdle;
			int                                   starts_ = 0;
			bool                                  came_up_ = false;  // the minecraft of the last start (or one found running) came up
			std::wstring                          failure_;
			std::chrono::steady_clock::time_point next_check_{};
		};

		MinecraftKeeper g_keeper;

		// above the engine's other panels under the root, the menus (gameui) included
		constexpr int STATUS_Z = 1000;

		/// the keeper's status as a line at the top of the screen, over the menus too.
		class StatusPanel : public vgui::Panel
		{
			DECLARE_CLASS_SIMPLE(StatusPanel, vgui::Panel);

		public:
			explicit StatusPanel(vgui::VPANEL parent) : BaseClass(nullptr, "HalfCraftStatus")
			{
				SetParent(parent);
				SetZPos(STATUS_Z);
				SetPaintBackgroundEnabled(false);
				SetMouseInputEnabled(false);
				SetKeyBoardInputEnabled(false);
				SetVisible(true);
			}

		protected:
			void OnThink() override
			{
				int wide, tall;
				engine->GetScreenSize(wide, tall);
				SetBounds(0, 0, wide, tall);
			}

			void Paint() override
			{
				const wchar_t* text = g_keeper.status();
				if (!text) {
					return;
				}
				int wide, tall;
				GetSize(wide, tall);
				const int font_tall = std::max(14, tall / 40);
				if (font_tall != font_tall_) {
					font_ = vgui::surface()->CreateFont();
					vgui::surface()->SetFontGlyphSet(font_, "Verdana", font_tall, 700, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS);
					font_tall_ = font_tall;
				}
				const int length = static_cast<int>(std::wcslen(text));
				int       text_wide, text_tall;
				vgui::surface()->GetTextSize(font_, text, text_wide, text_tall);
				const int pad = font_tall / 2;
				const int x = (wide - text_wide) / 2;
				const int y = tall / 14;
				vgui::surface()->DrawSetColor(0, 0, 0, 170);
				vgui::surface()->DrawFilledRect(x - pad, y - pad / 2, x + text_wide + pad, y + text_tall + pad / 2);
				vgui::surface()->DrawSetTextFont(font_);
				vgui::surface()->DrawSetTextColor(255, 210, 110, 255);
				vgui::surface()->DrawSetTextPos(x, y);
				vgui::surface()->DrawPrintText(text, length);
			}

		private:
			vgui::HFont font_ = vgui::INVALID_FONT;
			int         font_tall_ = 0;
		};

		class MinecraftSystem final : public CAutoGameSystemPerFrame
		{
		public:
			MinecraftSystem() : CAutoGameSystemPerFrame("HalfCraftMinecraft") {}

			void PostInit() override
			{
				g_keeper.init(engine->GetGameDirectory());
				// straight under the root, above the menus: the client's own panels are drawn beneath them
				panel_ = new StatusPanel(enginevgui->GetPanel(PANEL_ROOT));
			}

			void Shutdown() override
			{
				delete panel_;
				panel_ = nullptr;
			}

			void Update(float) override
			{
				auto& s = client_session();
				g_keeper.update(s.link_ready && s.link.mc_alive());
			}

		private:
			StatusPanel* panel_ = nullptr;
		};

		MinecraftSystem g_minecraft_system;
	}
}
