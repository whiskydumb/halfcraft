// client.dll: minecraft's hand, hotbar, hearts and every open minecraft screen, drawn over source's
// frame as a hud element (so source's own menus and console still go on top).
//
// minecraft renders them offscreen at source's resolution into the overlay triple buffer, rgba8 with
// premultiplied alpha. vgui blends straight alpha, so each new frame is un-premultiplied on upload.
// vgui textures are power-of-two sized: the frame sits in the top-left of one, the rest transparent.

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

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
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

	int next_power_of_two(int value)
	{
		int result = 1;
		while (result < value) {
			result <<= 1;
		}
		return result;
	}

	void unpremultiply(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels)
	{
		const auto& table = unpremultiply_table();
		for (std::size_t i = 0; i < pixels; ++i, src += 4, dst += 4) {
			const std::uint32_t a = src[3];
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
	}
}

class CHudHalfCraftOverlay : public vgui::Panel, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CHudHalfCraftOverlay, vgui::Panel );

public:
	explicit CHudHalfCraftOverlay( const char *pElementName );

	bool ShouldDraw( void ) override;

protected:
	void ApplySchemeSettings( vgui::IScheme *pScheme ) override;
	void OnThink( void ) override;
	void Paint( void ) override;

private:
	int								m_nTextureId = -1;
	int								m_nWidth = 0;			// the frame
	int								m_nHeight = 0;
	int								m_nTextureWide = 0;	// the power-of-two texture it sits in
	int								m_nTextureTall = 0;
	bool							m_bBottomUp = false;
	bool							m_bHaveFrame = false;
	std::vector<std::uint8_t>		m_Pixels;
};

DECLARE_HUDELEMENT( CHudHalfCraftOverlay );

CHudHalfCraftOverlay::CHudHalfCraftOverlay( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudHalfCraftOverlay" )
{
	SetParent( g_pClientMode->GetViewport() );
	SetHiddenBits( 0 );  // minecraft's hud shows whatever source's hud is doing
}

void CHudHalfCraftOverlay::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetPaintBackgroundEnabled( false );
	SetMouseInputEnabled( false );
	SetKeyBoardInputEnabled( false );
	SetZPos( 1000 );
}

void CHudHalfCraftOverlay::OnThink( void )
{
	int nWide, nTall;
	engine->GetScreenSize( nWide, nTall );
	SetBounds( 0, 0, nWide, nTall );
}

bool CHudHalfCraftOverlay::ShouldDraw( void )
{
	const auto &session = halfcraft::client_session();
	return session.link_ready && session.minecraft_hud && CHudElement::ShouldDraw();
}

void CHudHalfCraftOverlay::Paint( void )
{
	auto &session = halfcraft::client_session();
	if ( m_nTextureId < 0 )
	{
		m_nTextureId = vgui::surface()->CreateNewTextureID( true );
	}

	if ( session.link.acquire_overlay_frame() )
	{
		const auto *pHeader = session.link.front_header();
		const int nWidth = static_cast<int>( pHeader->width );
		const int nHeight = static_cast<int>( pHeader->height );
		if ( nWidth > 0 && nHeight > 0 && nWidth <= static_cast<int>( halfcraft::proto::kMaxOverlayW ) && nHeight <= static_cast<int>( halfcraft::proto::kMaxOverlayH ) )
		{
			const int nTextureWide = next_power_of_two( nWidth );
			const int nTextureTall = next_power_of_two( nHeight );
			if ( nTextureWide != m_nTextureWide || nTextureTall != m_nTextureTall )
			{
				m_Pixels.assign( static_cast<std::size_t>( nTextureWide ) * nTextureTall * 4, 0 );
				m_nTextureWide = nTextureWide;
				m_nTextureTall = nTextureTall;
			}
			else if ( nWidth < m_nWidth || nHeight < m_nHeight )
			{
				std::fill( m_Pixels.begin(), m_Pixels.end(), static_cast<std::uint8_t>( 0 ) );  // the last, bigger frame mustn't show around this one
			}
			const std::uint8_t *pSource = session.link.front_pixels();
			for ( int y = 0; y < nHeight; ++y )
			{
				unpremultiply( pSource + static_cast<std::size_t>( y ) * nWidth * 4, m_Pixels.data() + static_cast<std::size_t>( y ) * nTextureWide * 4, static_cast<std::size_t>( nWidth ) );
			}
			vgui::surface()->DrawSetTextureRGBA( m_nTextureId, m_Pixels.data(), nTextureWide, nTextureTall, false, true );
			m_nWidth = nWidth;
			m_nHeight = nHeight;
			m_bBottomUp = ( pHeader->flags & 1 ) != 0;
			m_bHaveFrame = true;
		}
	}
	if ( !m_bHaveFrame )
		return;

	int nWide, nTall;
	GetSize( nWide, nTall );
	vgui::surface()->DrawSetColor( 255, 255, 255, 255 );
	vgui::surface()->DrawSetTexture( m_nTextureId );
	const float flRight = static_cast<float>( m_nWidth ) / m_nTextureWide;
	const float flBottom = static_cast<float>( m_nHeight ) / m_nTextureTall;
	if ( m_bBottomUp )
		vgui::surface()->DrawTexturedSubRect( 0, 0, nWide, nTall, 0.0f, flBottom, flRight, 0.0f );
	else
		vgui::surface()->DrawTexturedSubRect( 0, 0, nWide, nTall, 0.0f, 0.0f, flRight, flBottom );
}
