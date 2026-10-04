// client.dll: keyboard and mouse while minecraft has them. source still gets a short allow-list:
// the console, its pause menu (esc, when no minecraft screen is open), quicksave/quickload, and two
// keys of its own: G uses things (doors, buttons, chargers) and V toggles the suit flashlight.

#include "cbase.h"
#include "in_buttons.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>

#include "client/hc_client.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr int SDL_NONE = 0;
		constexpr int WHEEL_NOTCH = 120;

		/// source key -> SDL scancode (usb hid usage, what minecraft 26.x reads).
		int sdl_scancode(ButtonCode_t code)
		{
			if (code >= KEY_A && code <= KEY_Z) {
				return 4 + (code - KEY_A);
			}
			if (code >= KEY_1 && code <= KEY_9) {
				return 30 + (code - KEY_1);
			}
			if (code >= KEY_PAD_1 && code <= KEY_PAD_9) {
				return 89 + (code - KEY_PAD_1);
			}
			if (code >= KEY_F1 && code <= KEY_F12) {
				return 58 + (code - KEY_F1);
			}
			switch (code) {
			case KEY_0: return 39;
			case KEY_PAD_0: return 98;
			case KEY_PAD_DIVIDE: return 84;
			case KEY_PAD_MULTIPLY: return 85;
			case KEY_PAD_MINUS: return 86;
			case KEY_PAD_PLUS: return 87;
			case KEY_PAD_ENTER: return 88;
			case KEY_PAD_DECIMAL: return 99;
			case KEY_LBRACKET: return 47;
			case KEY_RBRACKET: return 48;
			case KEY_SEMICOLON: return 51;
			case KEY_APOSTROPHE: return 52;
			case KEY_BACKQUOTE: return 53;
			case KEY_COMMA: return 54;
			case KEY_PERIOD: return 55;
			case KEY_SLASH: return 56;
			case KEY_BACKSLASH: return 49;
			case KEY_MINUS: return 45;
			case KEY_EQUAL: return 46;
			case KEY_ENTER: return 40;
			case KEY_SPACE: return 44;
			case KEY_BACKSPACE: return 42;
			case KEY_TAB: return 43;
			case KEY_CAPSLOCK: return 57;
			case KEY_NUMLOCK: return 83;
			case KEY_ESCAPE: return 41;
			case KEY_SCROLLLOCK: return 71;
			case KEY_INSERT: return 73;
			case KEY_DELETE: return 76;
			case KEY_HOME: return 74;
			case KEY_END: return 77;
			case KEY_PAGEUP: return 75;
			case KEY_PAGEDOWN: return 78;
			case KEY_BREAK: return 72;
			case KEY_LSHIFT: return 225;
			case KEY_RSHIFT: return 229;
			case KEY_LALT: return 226;
			case KEY_RALT: return 230;
			case KEY_LCONTROL: return 224;
			case KEY_RCONTROL: return 228;
			case KEY_LWIN: return 227;
			case KEY_RWIN: return 231;
			case KEY_APP: return 101;
			case KEY_UP: return 82;
			case KEY_LEFT: return 80;
			case KEY_DOWN: return 81;
			case KEY_RIGHT: return 79;
			default: return SDL_NONE;
			}
		}

		/// source mouse button -> SDL button (1 left, 2 middle, 3 right, 4 x1, 5 x2).
		int sdl_button(ButtonCode_t code)
		{
			switch (code) {
			case MOUSE_LEFT: return 1;
			case MOUSE_MIDDLE: return 2;
			case MOUSE_RIGHT: return 3;
			case MOUSE_4: return 4;
			case MOUSE_5: return 5;
			default: return 0;
			}
		}

		/// the character a key types on a us layout (minecraft's text fields need characters, not
		/// keys). 0 when it types nothing.
		int typed_char(ButtonCode_t code, bool shift)
		{
			if (code >= KEY_A && code <= KEY_Z) {
				return (shift ? 'A' : 'a') + (code - KEY_A);
			}
			if (code >= KEY_0 && code <= KEY_9) {
				static constexpr char SHIFTED[] = ")!@#$%^&*(";
				return shift ? SHIFTED[code - KEY_0] : '0' + (code - KEY_0);
			}
			if (code >= KEY_PAD_0 && code <= KEY_PAD_9) {
				return '0' + (code - KEY_PAD_0);
			}
			switch (code) {
			case KEY_SPACE: return ' ';
			case KEY_LBRACKET: return shift ? '{' : '[';
			case KEY_RBRACKET: return shift ? '}' : ']';
			case KEY_SEMICOLON: return shift ? ':' : ';';
			case KEY_APOSTROPHE: return shift ? '"' : '\'';
			case KEY_BACKQUOTE: return shift ? '~' : '`';
			case KEY_COMMA: return shift ? '<' : ',';
			case KEY_PERIOD: return shift ? '>' : '.';
			case KEY_SLASH: return shift ? '?' : '/';
			case KEY_BACKSLASH: return shift ? '|' : '\\';
			case KEY_MINUS: return shift ? '_' : '-';
			case KEY_EQUAL: return shift ? '+' : '=';
			case KEY_PAD_DIVIDE: return '/';
			case KEY_PAD_MULTIPLY: return '*';
			case KEY_PAD_MINUS: return '-';
			case KEY_PAD_PLUS: return '+';
			case KEY_PAD_DECIMAL: return '.';
			default: return 0;
			}
		}

		/// keys source keeps even while minecraft has the keyboard.
		bool source_keeps(ButtonCode_t code, const ClientSession& s)
		{
			switch (code) {
			case KEY_BACKQUOTE:  // console
			case KEY_F6:         // quicksave
			case KEY_F7:
			case KEY_F9:         // quickload
			case KEY_F10:        // quit
				return true;
			case KEY_ESCAPE:     // the engine routes it through client_ui_toggle
				return true;
			default:
				return false;
			}
		}

		bool g_shift_down = false;
		int  g_source_buttons = 0;  // mouse buttons (sdl numbers as bits) whose press went to source: their release does too

		/// source's use: G, or any key source binds +use to (its own keyboard options)
		bool is_use_key(ButtonCode_t code, const char* binding)
		{
			return code == KEY_G || (binding && !Q_stricmp(binding, "+use"));
		}
	}

	void input_release_all(ClientSession& session)
	{
		session.use_held = false;
		session.forward_held = false;
		session.attack_held = false;
		session.attack2_held = false;
		g_source_buttons = 0;
		g_shift_down = false;
		session.link.push_input(proto::kInReleaseAll, 0);
	}

	bool client_key_event(int down, ButtonCode_t code, const char* binding)
	{
		auto& s = client_session();
		if (!s.minecraft_owns_input || source_keeps(code, s)) {
			return true;
		}
		const bool pressed = down != 0;

		// source's own two
		if (is_use_key(code, binding) && !s.mc_screen_open) {
			s.use_held = pressed;
			return false;
		}
		if (code == KEY_V) {
			if (pressed) {
				engine->ClientCmd_Unrestricted("impulse 100");
			}
			return false;
		}

		if (const int button = sdl_button(code)) {
			// carrying a prop: left throws it, right drops it (half-life's attack buttons)
			const int bit = 1 << button;
			const bool to_source = pressed ? (s.holding && !s.mc_screen_open && (button == 1 || button == 3)) : (g_source_buttons & bit) != 0;
			if (to_source) {
				g_source_buttons = pressed ? (g_source_buttons | bit) : (g_source_buttons & ~bit);
				(button == 1 ? s.attack_held : s.attack2_held) = pressed;
				return false;
			}
			s.link.push_input(proto::kInMouseButton, static_cast<std::uint16_t>(button), pressed ? 1 : 0);
			return false;
		}
		if (code == MOUSE_WHEEL_UP || code == MOUSE_WHEEL_DOWN) {
			if (pressed) {
				s.link.push_input(proto::kInScroll, 0, code == MOUSE_WHEEL_UP ? WHEEL_NOTCH : -WHEEL_NOTCH);
			}
			return false;
		}
		if (code == KEY_LSHIFT || code == KEY_RSHIFT) {
			g_shift_down = pressed;
		}
		if (code == KEY_W) {
			s.forward_held = pressed;  // still minecraft's; source only watches it for ladders
		}
		if (const int scancode = sdl_scancode(code)) {
			s.link.push_input(proto::kInKey, static_cast<std::uint16_t>(scancode), pressed ? 1 : 0);
			if (pressed && s.mc_screen_open) {
				if (const int ch = typed_char(code, g_shift_down)) {
					s.link.push_input(proto::kInText, 0, ch);
				}
			}
		}
		return false;  // minecraft's, even keys it doesn't know (joystick, ...)
	}

	bool client_ui_toggle()
	{
		auto& s = client_session();
		if (!s.minecraft_owns_input || !s.mc_screen_open) {
			return false;
		}
		constexpr std::uint16_t SDL_ESCAPE = 41;
		s.link.push_input(proto::kInKey, SDL_ESCAPE, 1);
		s.link.push_input(proto::kInKey, SDL_ESCAPE, 0);
		return true;
	}

	bool client_mouse_move(float mouse_x, float mouse_y, QAngle& view_angles)
	{
		auto& s = client_session();
		if (!s.minecraft_owns_input) {
			return false;
		}
		if (s.mc_screen_open) {
			// minecraft's cursor, one overlay pixel per mouse count
			s.cursor_x = std::clamp(s.cursor_x + static_cast<int>(std::lround(mouse_x)), 0, std::max(0, s.viewport_w - 1));
			s.cursor_y = std::clamp(s.cursor_y + static_cast<int>(std::lround(mouse_y)), 0, std::max(0, s.viewport_h - 1));
			if (mouse_x != 0.0f || mouse_y != 0.0f) {
				s.link.push_input(proto::kInCursor, 0, s.cursor_x, s.cursor_y);
			}
		} else {
			// minecraft's own mouse-look formula (MouseHandler.turnPlayer)
			const float sens = s.sensitivity * 0.6f + 0.2f;
			const float factor = sens * sens * sens * 8.0f * 0.15f;
			s.yaw = wrap_degrees(s.yaw + mouse_x * factor);
			s.pitch = std::clamp(s.pitch + mouse_y * factor, -90.0f, 90.0f);
		}
		view_angles.Init(s.pitch, mc_yaw_to_source(s.yaw), 0.0f);
		return true;
	}
}

// developer helpers: synthetic input (remote desktop, automation) reaches source's raw mouse as
// absolute positions, which make useless look deltas.
CON_COMMAND( hc_look, "halfcraft: point minecraft's look: hc_look <pitch> <yaw> (minecraft degrees)" )
{
	if ( args.ArgC() < 3 )
	{
		Msg( "usage: hc_look <pitch> <yaw>\n" );
		return;
	}
	auto &s = halfcraft::client_session();
	s.pitch = std::clamp( static_cast<float>( atof( args[1] ) ), -90.0f, 90.0f );
	s.yaw = halfcraft::wrap_degrees( static_cast<float>( atof( args[2] ) ) );
}

CON_COMMAND( hc_click, "halfcraft: click a minecraft mouse button: hc_click <1 left | 2 middle | 3 right>" )
{
	const int nButton = args.ArgC() > 1 ? atoi( args[1] ) : 1;
	if ( nButton < 1 || nButton > 5 )
		return;
	auto &s = halfcraft::client_session();
	s.link.push_input( halfcraft::proto::kInMouseButton, static_cast<std::uint16_t>( nButton ), 1 );
	s.link.push_input( halfcraft::proto::kInMouseButton, static_cast<std::uint16_t>( nButton ), 0 );
}
