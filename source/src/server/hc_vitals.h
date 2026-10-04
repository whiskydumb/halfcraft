#pragma once

// server.dll: minecraft owns the player's health whenever its player is in its world (also while
// source has the player on a ladder or a ride): every hit goes to minecraft, so source's own health
// would sit at full and its health kits, wall chargers, suit batteries and medics would never help.
// minecraft's health and absorption are mirrored onto half-life's player instead, so they see it
// and work as usual; whatever they add goes to minecraft (health heals, suit armour becomes
// absorption).

#include "core/hc_link.h"

class CBasePlayer;

namespace halfcraft
{
	class Vitals
	{
	public:
		/// once per frame.
		void update(Link& link, CBasePlayer* player);

		/// a level loaded: source's player comes with the health it had (a save, a transition), which
		/// isn't healing to pass on.
		void reset()
		{
			health_.reset();
			armor_.reset();
		}

	private:
		struct Mirror
		{
			int   set = -1;         // what source's player was given last (-1: nothing yet)
			float pending = 0.0f;   // healed in half-life, not yet seen in minecraft (source points)
			float seen = -1.0f;     // minecraft's value as last read (minecraft points)
			float waited = 0.0f;    // seconds the pending amount has gone unanswered

			/// forwards what half-life added since the last frame and returns the value to give source's
			/// player now.
			int update(int current, float minecraft, int kind, float frametime);
			void reset() { *this = Mirror{}; }
		};

		Mirror health_;
		Mirror armor_;
	};

	/// whether minecraft owns the player's health this frame (half-life's hits go to it).
	bool minecraft_owns_health();
}
