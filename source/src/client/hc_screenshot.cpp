// client.dll: minecraft's screenshot key. minecraft asks through server.dll (proto::kEvScreenshot ->
// HalfCraft_RequestScreenshot, hc_bridge.h), or hc_screenshot asks here. the next finished frame (the
// world, the hud and minecraft's overlay; source's menus and console are drawn after it) is read back
// and written to a temporary file, and minecraft gets its path on proto::kStrScreenshot: it saves the
// png itself, named and announced like its own screenshots.

#include "cbase.h"
#include "materialsystem/imaterialsystem.h"
#include "tier1/callqueue.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "client/hc_client.h"
#include "core/hc_log.h"
#include "shared/hc_bridge.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		// asked, and no frame finished since: source isn't drawing a world (no map, a loading screen)
		constexpr double        FRAME_TIMEOUT_SECONDS = 2.0;
		// read back and still not done: a wedged render thread, or a call queue that was dropped. both
		// timeouts together stay under minecraft's 5 s, so it prints the reason rather than its own
		constexpr double        CAPTURE_TIMEOUT_SECONDS = 2.0;
		constexpr std::uint32_t HOST_REQUEST = 0;  // hc_screenshot's, not minecraft's

		/// one read-back of the back buffer.
		struct Capture
		{
			int                        width = 0;
			int                        height = 0;
			std::vector<unsigned char> pixels;  // RGB8, top row first
			std::atomic<bool>          done{ false };
		};

		/// runs where the material system draws: on the main thread, or in order on its render thread
		/// when it queues (mat_queue_mode), right after the frame's vgui either way.
		void read_back(Capture* capture)
		{
			CMatRenderContextPtr context(materials);
			context->ReadPixels(0, 0, capture->width, capture->height, capture->pixels.data(), IMAGE_FORMAT_RGB888);
			capture->done.store(true, std::memory_order_release);
		}

		class Screenshots
		{
		public:
			/// any thread: server.dll's frame needn't run on ours.
			void request(std::uint32_t request)
			{
				std::lock_guard<std::mutex> lock(lock_);
				incoming_.push_back(request);
			}

			/// every frame, before it's drawn.
			void update(ClientSession& s);
			/// the frame is finished (PostRenderVGui).
			void frame_finished();
			void shutdown();

		private:
			void finish(ClientSession& s);
			void abandon_capture(ClientSession& s);
			void fail_all(ClientSession& s, const char* reason);
			/// @return false when minecraft can't be told
			static bool answer(ClientSession& s, std::uint32_t request, const std::string& text);

			std::mutex                 lock_;
			std::vector<std::uint32_t> incoming_;
			std::vector<std::uint32_t> waiting_;  // taken, not answered yet
			double                     asked_at_ = 0.0;
			bool                       want_frame_ = false;
			std::unique_ptr<Capture>   capture_;
			double                     captured_at_ = 0.0;
			// given up on, but the render thread may still write them: freed once it has
			std::vector<std::unique_ptr<Capture>> abandoned_;
			std::uint32_t              files_ = 0;
		};

		Screenshots g_screenshots;

		void Screenshots::update(ClientSession& s)
		{
			{
				std::lock_guard<std::mutex> lock(lock_);
				for (const auto request : incoming_) {
					log_info("screenshot %u: asked", request);
					waiting_.push_back(request);
				}
				incoming_.clear();
			}
			abandoned_.erase(std::remove_if(abandoned_.begin(), abandoned_.end(), [](const std::unique_ptr<Capture>& capture) { return capture->done.load(std::memory_order_acquire); }), abandoned_.end());
			if (waiting_.empty()) {
				return;
			}
			// one frame answers everyone waiting, also whoever asked while it was being read back
			if (capture_) {
				if (capture_->done.load(std::memory_order_acquire)) {
					finish(s);
				} else if (Plat_FloatTime() - captured_at_ > CAPTURE_TIMEOUT_SECONDS) {
					abandon_capture(s);
				}
				return;
			}
			if (s.loading) {
				want_frame_ = false;
				fail_all(s, "Half-Life is loading or has no map");
				return;
			}
			if (!want_frame_) {
				want_frame_ = true;
				asked_at_ = Plat_FloatTime();
			} else if (Plat_FloatTime() - asked_at_ > FRAME_TIMEOUT_SECONDS) {
				want_frame_ = false;
				fail_all(s, "Half-Life drew no frame");
			}
		}

		void Screenshots::frame_finished()
		{
			if (!want_frame_ || capture_) {
				return;
			}
			int width = 0, height = 0;
			materials->GetBackBufferDimensions(width, height);
			if (width <= 0 || height <= 0) {
				return;  // no back buffer to read: the request times out
			}
			want_frame_ = false;
			capture_ = std::make_unique<Capture>();
			capture_->width = width;
			capture_->height = height;
			capture_->pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
			captured_at_ = Plat_FloatTime();

			CMatRenderContextPtr context(materials);
			ICallQueue*          queue = context->GetCallQueue();
			log_info("screenshot: reading the finished %dx%d frame back%s", width, height, queue ? " (on the render thread)" : "");
			if (queue) {
				queue->QueueCall(read_back, capture_.get());
			} else {
				read_back(capture_.get());
			}
		}

		void Screenshots::finish(ClientSession& s)
		{
			const std::unique_ptr<Capture> capture = std::move(capture_);
			std::error_code                error;
			const auto                     folder = std::filesystem::temp_directory_path(error);
			if (error) {
				fail_all(s, "no temporary folder to put the frame in");
				return;
			}
			for (const auto request : waiting_) {
				// a file each: minecraft deletes it once it has saved the png
				const auto path = folder / ("halfcraft-screenshot-" + std::to_string(qpc_now()) + "-" + std::to_string(++files_) + ".rgb");
				std::ofstream file(path, std::ios::binary | std::ios::trunc);
				file.write(reinterpret_cast<const char*>(capture->pixels.data()), static_cast<std::streamsize>(capture->pixels.size()));
				file.close();
				if (!file) {
					std::filesystem::remove(path, error);
					log_warning("screenshot %u failed: couldn't write %s", request, path.u8string().c_str());
					answer(s, request, "fail " + std::to_string(request) + " couldn't write the frame to the temporary folder");
					continue;
				}
				log_info("screenshot %u: the %dx%d frame is in %s", request, capture->width, capture->height, path.u8string().c_str());
				const std::string text = "ok " + std::to_string(request) + " " + std::to_string(capture->width) + " " + std::to_string(capture->height) + " " + path.u8string();
				if (!answer(s, request, text)) {
					std::filesystem::remove(path, error);
				}
			}
			waiting_.clear();
		}

		void Screenshots::abandon_capture(ClientSession& s)
		{
			log_warning("screenshot: the %dx%d frame never came back from the render thread", capture_->width, capture_->height);
			abandoned_.push_back(std::move(capture_));
			fail_all(s, "Half-Life's frame never came back");
		}

		void Screenshots::fail_all(ClientSession& s, const char* reason)
		{
			for (const auto request : waiting_) {
				log_warning("screenshot %u failed: %s", request, reason);
				answer(s, request, "fail " + std::to_string(request) + " " + reason);
			}
			waiting_.clear();
		}

		bool Screenshots::answer(ClientSession& s, std::uint32_t request, const std::string& text)
		{
			if (s.link_ready && s.link.push_string(proto::kStrScreenshot, text)) {
				return true;
			}
			log_warning("screenshot %u: minecraft can't be told (no link, or its input ring is full)", request);
			return false;
		}

		void Screenshots::shutdown()
		{
			// the render thread may still write the ones not done: they go with the process
			if (capture_) {
				abandoned_.push_back(std::move(capture_));
			}
			for (auto& capture : abandoned_) {
				if (!capture->done.load(std::memory_order_acquire)) {
					static_cast<void>(capture.release());
				}
			}
			abandoned_.clear();
		}

		class ScreenshotSystem final : public CAutoGameSystemPerFrame
		{
		public:
			ScreenshotSystem() : CAutoGameSystemPerFrame("HalfCraftScreenshots") {}

			void Update(float) override { g_screenshots.update(client_session()); }
			void Shutdown() override { g_screenshots.shutdown(); }
		};

		ScreenshotSystem g_screenshot_system;
	}

	void client_post_render_vgui()
	{
		g_screenshots.frame_finished();
	}
}

// server.dll hands minecraft's screenshot key over (hc_bridge.h)
extern "C" __declspec(dllexport) void HalfCraft_RequestScreenshot(std::uint32_t request)
{
	halfcraft::g_screenshots.request(request);
}

CON_COMMAND( hc_screenshot, "halfcraft: save the next finished frame as a minecraft screenshot, like minecraft's screenshot key (F2)" )
{
	auto &s = halfcraft::client_session();
	if ( !s.link_ready || !s.link.mc_alive() )
	{
		halfcraft::log_warning( "hc_screenshot: minecraft isn't connected, so nothing would save the frame" );
		return;
	}
	halfcraft::g_screenshots.request( halfcraft::HOST_REQUEST );
}
