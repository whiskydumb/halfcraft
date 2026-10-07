// server.dll: half-life's weapons as minecraft items (see hc_weapons.h).

#include "cbase.h"
#include "ammodef.h"
#include "player.h"
#include "usercmd.h"
#include "hl2/weapon_physcannon.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/hc_log.h"
#include "server/hc_weapons.h"
#include "shared/hc_hooks.h"
#include "shared/hc_weapon_ids.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		// player_speedmod's SF_SPEED_MOD_SUPPRESS_WEAPONS (player.cpp)
		constexpr int SPEED_MOD_SUPPRESS_WEAPONS = 1 << 0;
		// how long a weapon source took out by itself stays out for minecraft's hand to move to it
		// (HostWeapons): a tick of minecraft's server and a command back, unless it isn't on the hotbar
		constexpr float FOLLOW_SECONDS = 1.0f;

		// weapons minecraft has no item for, said once each
		std::vector<std::string> g_unmirrored;
		// the weapon last said to stay away: Weapon_Equip and BumpWeapon both ask for one picked up
		int g_said_pickup_tick = -1;
		int g_said_pickup_weapon = 0;
		// a player_speedmod keeps the weapons away
		bool g_suppressed = false;

		/// the weapon out after the last frame, and one source took out by itself
		struct SourcePick
		{
			EHANDLE last_active;
			EHANDLE picked;
			float   until = 0.0f;
		};
		std::unordered_map<int, SourcePick> g_picks;  // by the player's entity index

		/// a player_speedmod keeps the player's weapons away. it slows the player meanwhile, so a respawn
		/// or a load that ended the scene leaves no flag behind.
		bool weapons_suppressed(CBasePlayer* player)
		{
			return g_suppressed && player->GetLaggedMovementValue() != 1.0f;
		}

		/// puts the weapon that's out away, with nothing in its place.
		bool put_away(CBasePlayer* player, CBaseCombatWeapon* active)
		{
			if (!active->CanHolster() || !active->Holster()) {
				return false;
			}
			player->ClearActiveWeapon();
			player->HideViewModels();
			return true;
		}

		/// the player's latest command, if minecraft's hand picked its weapon in it.
		const CUserCmd* minecraft_command(CBasePlayer* player)
		{
			const CUserCmd* cmd = player->GetLastUserCommand();
			return cmd && (cmd->hc_flags & HC_CMD_WEAPONS) ? cmd : nullptr;
		}

		/// the weapon a command's weaponselect names, if the player owns it.
		CBaseCombatWeapon* selected_weapon(CBasePlayer* player, const CUserCmd& cmd)
		{
			if (!cmd.weaponselect) {
				return nullptr;
			}
			auto* weapon = dynamic_cast<CBaseCombatWeapon*>(CBaseEntity::Instance(cmd.weaponselect));
			return weapon && weapon->GetOwner() == player ? weapon : nullptr;
		}

		/// source's active weapon follows minecraft's hand. taking one out is source's own weapon
		/// selection (weaponselect, in the command); what's left here is letting go and putting away.
		void follow_minecraft(CBasePlayer* player)
		{
			const CUserCmd*    cmd = minecraft_command(player);
			CBaseCombatWeapon* active = player->GetActiveWeapon();
			SourcePick&        pick = g_picks[player->entindex()];
			CBaseEntity*       before = pick.last_active.Get();
			// from one weapon to another: not one coming back out (a vehicle left, a speedmod ended)
			const bool switched = active && before && active != before;
			pick.last_active = active;
			if (!cmd || !active) {
				return;
			}
			// minecraft's hand stays as it is, the weapon stays away (a command on its way took it out)
			if (weapons_suppressed(player)) {
				if (put_away(player, active)) {
					pick.last_active = nullptr;
					log_info("weapons: %s put away (player_speedmod keeps the weapons away)", active->GetClassname());
				}
				return;
			}
			CBaseCombatWeapon* wanted = selected_weapon(player, *cmd);
			if (wanted == active) {
				pick.picked = nullptr;
				return;
			}
			// source took this one out by itself, as the one out ran dry or went away: it stays out while
			// minecraft's hand moves to it (HostWeapons)
			if (switched) {
				pick.picked = active;
				pick.until = gpGlobals->curtime + FOLLOW_SECONDS;
				log_info("weapons: source took %s out by itself; it stays out for minecraft's hand to follow", active->GetClassname());
			}
			if (pick.picked.Get() == active && gpGlobals->curtime < pick.until) {
				return;
			}
			// the gravity gun refuses to be switched away from while it holds something
			if (PhysCannonGetHeldEntity(active)) {
				PhysCannonForceDrop(active, nullptr);
				log_info("weapons: %s let go of what it held (minecraft's hand moved on)", active->GetClassname());
			}
			// the hand holds none of half-life's weapons, or one that can't come out (no ammo): nothing
			// is out. the rpg refuses while its rocket flies; the next frame asks again
			if ((!wanted || !player->Weapon_CanSwitchTo(wanted)) && put_away(player, active)) {
				pick.last_active = nullptr;
				log_info("weapons: %s put away (minecraft's hand holds %s)", active->GetClassname(),
					wanted ? wanted->GetClassname() : "none of half-life's weapons");
			}
		}

		void fill_record(CBasePlayer* player, CBaseCombatWeapon* weapon, proto::WeaponRecord& record)
		{
			const bool clip = weapon->UsesClipsForAmmo1();
			record.clip = clip ? weapon->Clip1() : -1;
			record.maxClip = clip ? weapon->GetMaxClip1() : -1;
			record.ammo = record.maxAmmo = -1;
			if (weapon->UsesPrimaryAmmo()) {
				record.ammo = player->GetAmmoCount(weapon->GetPrimaryAmmoType());
				record.maxAmmo = GetAmmoDef()->MaxCarry(weapon->GetPrimaryAmmoType());
			}
			record.ammo2 = record.maxAmmo2 = -1;
			if (weapon->UsesSecondaryAmmo()) {
				record.ammo2 = player->GetAmmoCount(weapon->GetSecondaryAmmoType());
				record.maxAmmo2 = GetAmmoDef()->MaxCarry(weapon->GetSecondaryAmmoType());
			}
			if (record.id == proto::kHostWeaponPhyscannon && PlayerHasMegaPhysCannon()) {
				record.flags |= proto::kWeaponRecordSupercharged;
			}
		}

		bool lists(const proto::WeaponTable& table, std::uint32_t id)
		{
			for (std::uint32_t i = 0; i < table.count; ++i) {
				if (table.weapons[i].id == id) {
					return true;
				}
			}
			return false;
		}

		const char* name_of(std::uint32_t id)
		{
			const char* classname = weapon_classname(id);
			return classname ? classname : "nothing";
		}

		/// what changed between two tables, one line each.
		void log_changes(const proto::WeaponTable& was, const proto::WeaponTable& now)
		{
			const bool was_live = (was.flags & proto::kWeaponTableLive) != 0;
			const bool live = (now.flags & proto::kWeaponTableLive) != 0;
			if (live != was_live) {
				if (live) {
					log_info("weapons: minecraft gets the player's %u weapons (%s out)", now.count, name_of(now.active));
				} else {
					log_info("weapons: no player; minecraft keeps its weapon items as they are");
				}
				return;
			}
			if (!live) {
				return;
			}
			for (std::uint32_t i = 0; i < now.count; ++i) {
				if (!lists(was, now.weapons[i].id)) {
					log_info("weapons: the player has %s now (%u weapons)", name_of(now.weapons[i].id), now.count);
				}
			}
			for (std::uint32_t i = 0; i < was.count; ++i) {
				if (!lists(now, was.weapons[i].id)) {
					log_info("weapons: %s is gone (%u weapons)", name_of(was.weapons[i].id), now.count);
				}
			}
			if (now.active != was.active) {
				log_info("weapons: %s out", name_of(now.active));
			}
		}
	}

	void Weapons::update(Link& link, CBasePlayer* player, bool minecraft_hud)
	{
		for (int i = 1; i <= gpGlobals->maxClients; ++i) {
			CBasePlayer* each = UTIL_PlayerByIndex(i);
			if (each && each->IsAlive()) {
				follow_minecraft(each);
			}
		}

		if (player) {
			// half-life's own hud has its viewmodel; minecraft's shows only the weapon minecraft holds
			const CUserCmd* cmd = player->GetLastUserCommand();
			const bool      show = !minecraft_hud || (cmd && (cmd->hc_flags & HC_CMD_VIEWMODEL));
			if (show != player->m_Local.m_bDrawViewmodel.Get()) {
				player->ShowViewModel(show);
			}
		}
		write_table(link, player);
	}

	void Weapons::write_table(Link& link, CBasePlayer* player)
	{
		proto::WeaponTable table{};
		// half-life's menu runs a background map with a player and no weapons: not the one playing
		if (player && player->IsAlive() && gpGlobals->eLoadType != MapLoad_Background) {
			table.flags = proto::kWeaponTableLive;
			for (int i = 0; i < player->WeaponCount() && table.count < proto::kMaxHostWeapons; ++i) {
				CBaseCombatWeapon* weapon = player->GetWeapon(i);
				if (!weapon) {
					continue;
				}
				const auto id = weapon_id(weapon->GetClassname());
				if (id == proto::kHostWeaponNone) {
					const std::string classname = weapon->GetClassname();
					if (std::find(g_unmirrored.begin(), g_unmirrored.end(), classname) == g_unmirrored.end()) {
						g_unmirrored.push_back(classname);
						log_warning("weapons: minecraft has no item for %s; it stays with source", classname.c_str());
					}
					continue;
				}
				auto& record = table.weapons[table.count++];
				record.id = id;
				fill_record(player, weapon, record);
			}
			CBaseCombatWeapon* active = player->GetActiveWeapon();
			table.active = active ? weapon_id(active->GetClassname()) : proto::kHostWeaponNone;
			if (weapons_suppressed(player)) {
				table.flags |= proto::kWeaponTableSuppressed;
			}
		}
		if (ever_sent_ && !std::memcmp(&table, &sent_, sizeof(table))) {
			return;
		}
		link.write_weapons(table);
		log_changes(sent_, table);
		sent_ = table;
		ever_sent_ = true;
	}

	void server_player_speed_mod(CBasePlayer* /*player*/, int flags, float speed)
	{
		if (!(flags & SPEED_MOD_SUPPRESS_WEAPONS)) {
			return;  // it leaves the weapons alone, and so does setting it back
		}
		const bool suppressed = speed != 1.0f;
		if (suppressed != g_suppressed) {
			log_info(suppressed ? "weapons: player_speedmod keeps the weapons away: minecraft's hand takes none out"
								: "weapons: player_speedmod gives the weapons back");
		}
		g_suppressed = suppressed;
	}

	bool server_minecraft_picks_weapon(CBasePlayer* player, CBaseCombatWeapon* weapon)
	{
		if (!minecraft_command(player)) {
			return false;
		}
		if (gpGlobals->tickcount != g_said_pickup_tick || weapon->entindex() != g_said_pickup_weapon) {
			g_said_pickup_tick = gpGlobals->tickcount;
			g_said_pickup_weapon = weapon->entindex();
			log_info("weapons: %s stays away (minecraft's hand picks what's out)", weapon->GetClassname());
		}
		return true;
	}
}

CON_COMMAND(hc_weapons, "halfcraft: list the player's weapons the way minecraft gets them (ammo, the one that's out)")
{
	CBasePlayer* player = UTIL_GetCommandClient();
	if (!player) {
		player = UTIL_GetLocalPlayer();
	}
	if (!player) {
		Msg("hc_weapons: no player\n");
		return;
	}
	CBaseCombatWeapon* active = player->GetActiveWeapon();
	halfcraft::log_info("hc_weapons: %s out", active ? active->GetClassname() : "nothing");
	for (int i = 0; i < player->WeaponCount(); ++i) {
		CBaseCombatWeapon* weapon = player->GetWeapon(i);
		if (!weapon) {
			continue;
		}
		halfcraft::proto::WeaponRecord record{};
		record.id = halfcraft::weapon_id(weapon->GetClassname());
		halfcraft::fill_record(player, weapon, record);
		halfcraft::log_info("hc_weapons: %s (minecraft id %u): clip %d/%d, ammo %d/%d, alt %d/%d%s", weapon->GetClassname(), record.id, record.clip,
			record.maxClip, record.ammo, record.maxAmmo, record.ammo2, record.maxAmmo2,
			(record.flags & halfcraft::proto::kWeaponRecordSupercharged) ? ", supercharged" : "");
	}
}
