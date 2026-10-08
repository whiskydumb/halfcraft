// client.dll: minecraft's hand, hotbar, hearts and every open minecraft screen, drawn over source's
// frame as a hud element (so source's own menus and console still go on top).
//
// minecraft renders them offscreen at source's resolution into the overlay triple buffer, rgba8 with
// premultiplied alpha. here a frame becomes a grid of TILE x TILE vgui textures. one texture for the
// whole screen was power-of-two sized (4096 wide past 2048 pixels, which vguimatsurface overran on
// some setups), and uploading it whole took most of a frame: 20 ms at 3440x1440. most of the frame is
// see-through and most of the rest stays put, so a tile is un-premultiplied (vgui blends straight
// alpha) and uploaded only when its pixels changed, and drawn only when something in it shows.

#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"

#include <vgui/ISurface.h>
#include <vgui_controls/Panel.h>

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "client/hc_client.h"
#include "client/hc_debug.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
	constexpr int TILE = 256;  // pixels; a power of two, as vgui textures are

	/// 255 * 256 / alpha, for un-premultiplying with a multiply and a shift.
	const std::array<std::uint32_t, 256>& unpremultiply_table()
	{
		static const auto table = [] {
			std::array<std::uint32_t, 256> t{};
			for (int a = 1; a < 256; ++a) {
				t[a] = (255u * 256u + a / 2) / a;
			}
			return t;
		}();
		return table;
	}

	/// @return whether any of the pixels shows (alpha above zero)
	bool unpremultiply(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels)
	{
		const auto&   table = unpremultiply_table();
		std::uint32_t any = 0;
		for (std::size_t i = 0; i < pixels; ++i, src += 4, dst += 4) {
			const std::uint32_t a = src[3];
			any |= a;
			if (a == 0) {
				std::memset(dst, 0, 4);
			} else if (a == 255) {
				std::memcpy(dst, src, 4);
			} else {
				const std::uint32_t k = table[a];
				dst[0] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255, (src[0] * k) >> 8));
				dst[1] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255, (src[1] * k) >> 8));
				dst[2] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255, (src[2] * k) >> 8));
				dst[3] = static_cast<std::uint8_t>(a);
			}
		}
		return any != 0;
	}

	/// minecraft's frames as a grid of TILE x TILE vgui textures, each uploaded again only when its
	/// part of the frame changed.
	class OverlayTiles
	{
	public:
		/// takes a new frame.
		/// @param pixels - width x height rgba8, premultiplied alpha
		/// @param bottom_up - the first row is the frame's bottom one
		/// @return how many tiles were uploaded
		int update(const std::uint8_t* pixels, int width, int height, bool bottom_up)
		{
			if (width != width_ || height != height_) {
				resize(width, height);
			}
			int uploaded = 0;
			for (int row = 0; row < rows_; ++row) {
				for (int column = 0; column < columns_; ++column) {
					Tile&             tile = tiles_[static_cast<std::size_t>(row) * columns_ + column];
					const int         x0 = column * TILE, y0 = row * TILE;
					const int         tile_wide = std::min(TILE, width - x0), tile_tall = std::min(TILE, height - y0);
					const std::size_t row_bytes = static_cast<std::size_t>(tile_wide) * 4;
					const auto        source_row = [&](int y) {
						const int frame_row = bottom_up ? height - 1 - (y0 + y) : y0 + y;
						return pixels + (static_cast<std::size_t>(frame_row) * width + x0) * 4;
					};
					bool changed = tile.last.empty();
					for (int y = 0; y < tile_tall && !changed; ++y) {
						changed = std::memcmp(source_row(y), tile.last.data() + y * row_bytes, row_bytes) != 0;
					}
					if (!changed) {
						continue;
					}
					tile.last.resize(row_bytes * tile_tall);
					tile.straight.resize(static_cast<std::size_t>(TILE) * TILE * 4);  // an edge tile's unused part stays transparent
					bool shows = false;
					for (int y = 0; y < tile_tall; ++y) {
						std::memcpy(tile.last.data() + y * row_bytes, source_row(y), row_bytes);
						shows |= unpremultiply(source_row(y), tile.straight.data() + static_cast<std::size_t>(y) * TILE * 4, static_cast<std::size_t>(tile_wide));
					}
					tile.shows = shows;
					if (shows) {
						vgui::surface()->DrawSetTextureRGBA(tile.texture, tile.straight.data(), TILE, TILE, false, true);
						++uploaded;
					}
				}
			}
			return uploaded;
		}

		/// stretches the last frame over the top-left wide x tall of the screen.
		void draw(int wide, int tall) const
		{
			if (width_ == 0) {
				return;
			}
			vgui::surface()->DrawSetColor(255, 255, 255, 255);
			// a tile edge lands on the same screen pixel for both neighbours: no gaps, no overlaps
			const auto screen_x = [&](int x) { return static_cast<int>(static_cast<long long>(x) * wide / width_); };
			const auto screen_y = [&](int y) { return static_cast<int>(static_cast<long long>(y) * tall / height_); };
			for (int row = 0; row < rows_; ++row) {
				for (int column = 0; column < columns_; ++column) {
					const Tile& tile = tiles_[static_cast<std::size_t>(row) * columns_ + column];
					if (!tile.shows) {
						continue;
					}
					const int x0 = column * TILE, y0 = row * TILE;
					const int x1 = std::min(x0 + TILE, width_), y1 = std::min(y0 + TILE, height_);
					vgui::surface()->DrawSetTexture(tile.texture);
					vgui::surface()->DrawTexturedSubRect(screen_x(x0), screen_y(y0), screen_x(x1), screen_y(y1), 0.0f, 0.0f, static_cast<float>(x1 - x0) / TILE,
						static_cast<float>(y1 - y0) / TILE);
				}
			}
		}

	private:
		struct Tile
		{
			int                       texture = -1;
			bool                      shows = false;  // a pixel isn't fully transparent
			std::vector<std::uint8_t> last;           // its part of the last frame, as minecraft sent it
			std::vector<std::uint8_t> straight;       // TILE x TILE, un-premultiplied: what its texture got
		};

		/// a grid for another frame size; every tile uploads afresh.
		void resize(int width, int height)
		{
			const int  columns = (width + TILE - 1) / TILE, rows = (height + TILE - 1) / TILE;
			const auto count = static_cast<std::size_t>(columns) * rows;
			for (std::size_t i = count; i < tiles_.size(); ++i) {
				vgui::surface()->DestroyTextureID(tiles_[i].texture);
			}
			tiles_.resize(count);
			for (Tile& tile : tiles_) {
				if (tile.texture < 0) {
					tile.texture = vgui::surface()->CreateNewTextureID(true);
				}
				tile.shows = false;
				tile.last.clear();
				tile.straight.clear();
			}
			width_ = width, height_ = height, columns_ = columns, rows_ = rows;
		}

		int               width_ = 0, height_ = 0;  // the frame
		int               columns_ = 0, rows_ = 0;
		std::vector<Tile> tiles_;  // row by row, the frame's top row first
	};

	/// hc_perf: the frame rate and what the overlay costs, over the last frames.
	class OverlayPerf
	{
	public:
		void frame(float frametime, float upload_ms, int textures)
		{
			samples_[next_++ % samples_.size()] = { frametime, upload_ms, textures };
		}

		void report() const
		{
			const std::size_t count = std::min(next_, samples_.size());
			if (count == 0) {
				Msg("halfcraft perf: no frames with minecraft's overlay yet\n");
				return;
			}
			double seconds = 0.0, upload_ms = 0.0, textures = 0.0;
			float  worst = 0.0f;
			for (std::size_t i = 0; i < count; ++i) {
				seconds += samples_[i].frametime;
				upload_ms += samples_[i].upload_ms;
				textures += samples_[i].textures;
				worst = std::max(worst, samples_[i].frametime);
			}
			Msg("halfcraft perf: %.1f fps (worst frame %.1f ms), overlay %.2f ms and %.1f texture uploads a frame, over %d frames\n", count / seconds, worst * 1000.0f,
				upload_ms / count, textures / count, static_cast<int>(count));
		}

		/// the overlay's average cost over the last frames it was drawn (ms; 0 before the first).
		float recent_upload_ms(std::size_t frames) const
		{
			const std::size_t count = std::min({ frames, next_, samples_.size() });
			float             total = 0.0f;
			for (std::size_t i = 0; i < count; ++i) {
				total += samples_[(next_ - 1 - i) % samples_.size()].upload_ms;
			}
			return count ? total / count : 0.0f;
		}

	private:
		struct Sample
		{
			float frametime = 0.0f;
			float upload_ms = 0.0f;
			int   textures = 0;
		};
		std::array<Sample, 300> samples_{};
		std::size_t             next_ = 0;
	};

	OverlayPerf g_perf;
}

class CHudHalfCraftOverlay : public vgui::Panel, public CHudElement
{
	DECLARE_CLASS_SIMPLE(CHudHalfCraftOverlay, vgui::Panel);

public:
	explicit CHudHalfCraftOverlay(const char* pElementName);

	bool ShouldDraw(void) override;

protected:
	void ApplySchemeSettings(vgui::IScheme* pScheme) override;
	void OnThink(void) override;
	void Paint(void) override;

private:
	OverlayTiles m_Tiles;
};

DECLARE_HUDELEMENT(CHudHalfCraftOverlay);

CHudHalfCraftOverlay::CHudHalfCraftOverlay(const char* pElementName) : CHudElement(pElementName), BaseClass(nullptr, "HudHalfCraftOverlay")
{
	SetParent(g_pClientMode->GetViewport());
	SetHiddenBits(0);  // minecraft's hud shows whatever source's hud is doing
}

void CHudHalfCraftOverlay::ApplySchemeSettings(vgui::IScheme* pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);
	SetPaintBackgroundEnabled(false);
	SetMouseInputEnabled(false);
	SetKeyBoardInputEnabled(false);
	SetZPos(1000);
}

void CHudHalfCraftOverlay::OnThink(void)
{
	int nWide, nTall;
	engine->GetScreenSize(nWide, nTall);
	SetBounds(0, 0, nWide, nTall);
}

bool CHudHalfCraftOverlay::ShouldDraw(void)
{
	const auto& session = halfcraft::client_session();
	return session.link_ready && session.minecraft_hud && CHudElement::ShouldDraw();
}

void CHudHalfCraftOverlay::Paint(void)
{
	auto&        session = halfcraft::client_session();
	const double flUploadStart = Plat_FloatTime();
	int          nUploads = 0;
	if (session.link.acquire_overlay_frame()) {
		const auto* pHeader = session.link.front_header();
		const int   nWidth = static_cast<int>(pHeader->width);
		const int   nHeight = static_cast<int>(pHeader->height);
		if (nWidth > 0 && nHeight > 0 && nWidth <= static_cast<int>(halfcraft::proto::kMaxOverlayW) && nHeight <= static_cast<int>(halfcraft::proto::kMaxOverlayH)) {
			nUploads = m_Tiles.update(session.link.front_pixels(), nWidth, nHeight, (pHeader->flags & 1) != 0);
		}
	}
	g_perf.frame(gpGlobals->absoluteframetime, static_cast<float>((Plat_FloatTime() - flUploadStart) * 1000.0), nUploads);

	int nWide, nTall;
	GetSize(nWide, nTall);
	m_Tiles.draw(nWide, nTall);
}

CON_COMMAND(hc_perf, "halfcraft: the frame rate and what minecraft's overlay costs, over the last 300 frames")
{
	g_perf.report();
}

float halfcraft::overlay_cost_ms(int frames)
{
	return g_perf.recent_upload_ms(static_cast<std::size_t>(std::max(frames, 0)));
}
