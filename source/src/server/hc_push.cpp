// server.dll: source's pushes on the player (see hc_push.h).

#include "cbase.h"
#include "player.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>

#include "core/hc_link.h"
#include "core/hc_log.h"
#include "core/hc_units.h"
#include "server/hc_push.h"
#include "shared/hc_bridge.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr float PUSH_SCALE = 1000.0f;  // proto::kInPush carries blocks per second * 1000
		// a point_push pushes every 0.05 s, not every frame, and the puppet's own moves take the flag off
		// in between: a gap this short is the same push going on (0.1 s flickered live, on d1_canals_01)
		constexpr float PUSH_GAP_SECONDS = 0.3f;
		// shoves can come every frame (a rollermine's shock): a line a second is plenty
		constexpr float SHOVE_LOG_SECONDS = 1.0f;

		// ServerImpulseQuiet's that are alive: source's own velocity corrections going on
		int   g_quiet = 0;
		float g_shove_logged = -1.0f;

		/// what pushes the player now (units per second): a trigger_push touched it this frame
		/// (FL_BASEVELOCITY), and the conveyor it stands on (CPlayerMove::CheckMovingGround adds that one
		/// before each move, and takes the flag off again)
		Vector current_push(CBasePlayer* player)
		{
			Vector       push = (player->GetFlags() & FL_BASEVELOCITY) ? player->GetBaseVelocity() : vec3_origin;
			CBaseEntity* ground = player->GetGroundEntity();
			if (ground && (ground->GetFlags() & FL_CONVEYOR)) {
				Vector belt;
				ground->GetGroundVelocityToApply(belt);
				push += belt;
			}
			return push;
		}

		int scaled(float units_per_second)
		{
			return static_cast<int>(std::lround(units_per_second / static_cast<float>(UNITS_PER_BLOCK) * PUSH_SCALE));
		}

		/// server.dll -> client.dll, which owns the input ring (hc_bridge.h)
		void send(proto::InputType type, const int push[3])
		{
			if (const PushInputFn push_input = client_push_input()) {
				push_input(type, 0, push[0], push[1], push[2]);
			}
		}
	}

	ServerImpulseQuiet::ServerImpulseQuiet()
	{
		++g_quiet;
	}

	ServerImpulseQuiet::~ServerImpulseQuiet()
	{
		--g_quiet;
	}

	void server_player_impulse(CBaseEntity* entity, const Vector& impulse)
	{
		CBasePlayer* player = ToBasePlayer(entity);
		if (g_quiet > 0 || !player || !player->IsAlive() || !server_player_puppeted()) {
			return;  // source's own correction, or source moves the player itself and keeps the shove
		}
		// minecraft axes: x east, y up (source z), z south (source -y)
		const int shove[3] = { scaled(impulse.x), scaled(impulse.z), scaled(-impulse.y) };
		if (shove[0] == 0 && shove[1] == 0 && shove[2] == 0) {
			return;
		}
		const float time = gpGlobals->curtime;
		if (g_shove_logged < 0.0f || time < g_shove_logged || time - g_shove_logged >= SHOVE_LOG_SECONDS) {
			g_shove_logged = time;
			log_info("source shoves the player (%.2f %.2f %.2f blocks a second): minecraft's player takes it as momentum", shove[0] / PUSH_SCALE,
				shove[1] / PUSH_SCALE, shove[2] / PUSH_SCALE);
		}
		send(proto::kInImpulse, shove);
	}

	void Push::update(CBasePlayer* player, bool puppeted)
	{
		// minecraft axes: x east, y up (source z), z south (source -y)
		int         now[3] = { 0, 0, 0 };
		const float time = gpGlobals->curtime;
		if (player && puppeted && player->IsAlive()) {
			const Vector push = current_push(player);
			now[0] = scaled(push.x);
			now[1] = scaled(push.z);
			now[2] = scaled(-push.y);
			if (now[0] != 0 || now[1] != 0 || now[2] != 0) {
				std::copy(now, now + 3, held_);
				held_at_ = time;
			} else if (held_at_ >= 0.0f && time >= held_at_ && time - held_at_ < PUSH_GAP_SECONDS) {
				std::copy(held_, held_ + 3, now);
			}
		}
		const bool pushing = now[0] != 0 || now[1] != 0 || now[2] != 0;
		const bool was_pushing = sent_[0] != 0 || sent_[1] != 0 || sent_[2] != 0;
		const bool changed = now[0] != sent_[0] || now[1] != sent_[1] || now[2] != sent_[2];
		// repeated while it lasts, so a minecraft that missed the stop (a restart) lets go by itself; the
		// clock starts over with every map
		const float repeat = static_cast<float>(proto::kPushRepeatMs) / 1000.0f;
		const bool  due = pushing && (sent_at_ < 0.0f || time < sent_at_ || time - sent_at_ >= repeat);
		if (!changed && !due) {
			return;
		}
		if (pushing != was_pushing) {
			if (pushing) {
				log_info("source pushes the player (%.2f %.2f %.2f blocks a second): minecraft's player moves along", now[0] / PUSH_SCALE, now[1] / PUSH_SCALE,
					now[2] / PUSH_SCALE);
			} else {
				log_info("source stopped pushing the player");
			}
		}
		send(proto::kInPush, now);
		std::copy(now, now + 3, sent_);
		sent_at_ = time;
	}
}
