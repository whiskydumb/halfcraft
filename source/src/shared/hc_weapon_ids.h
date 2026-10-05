#pragma once

// half-life's weapons by the ids minecraft knows them by (proto::WeaponId), both ways. client.dll finds
// the weapon minecraft's hand holds by it, server.dll lists the player's weapons with it.

#include <cstdint>
#include <cstring>

#include "halfcraft_protocol.h"

namespace halfcraft
{
	struct WeaponName
	{
		proto::WeaponId id;
		const char*     classname;
	};

	inline constexpr WeaponName WEAPON_NAMES[] = {
		{ proto::kHostWeaponCrowbar, "weapon_crowbar" },
		{ proto::kHostWeaponPhyscannon, "weapon_physcannon" },
		{ proto::kHostWeaponPistol, "weapon_pistol" },
		{ proto::kHostWeapon357, "weapon_357" },
		{ proto::kHostWeaponSmg1, "weapon_smg1" },
		{ proto::kHostWeaponAr2, "weapon_ar2" },
		{ proto::kHostWeaponShotgun, "weapon_shotgun" },
		{ proto::kHostWeaponCrossbow, "weapon_crossbow" },
		{ proto::kHostWeaponFrag, "weapon_frag" },
		{ proto::kHostWeaponRpg, "weapon_rpg" },
		{ proto::kHostWeaponBugbait, "weapon_bugbait" },
	};

	/// @param classname - a weapon's classname (CBaseCombatWeapon::GetName)
	/// @return kHostWeaponNone for one minecraft has no item for
	inline proto::WeaponId weapon_id(const char* classname)
	{
		if (classname) {
			for (const auto& name : WEAPON_NAMES) {
				if (!std::strcmp(classname, name.classname)) {
					return name.id;
				}
			}
		}
		return proto::kHostWeaponNone;
	}

	/// @return nullptr for an id no weapon has
	inline const char* weapon_classname(std::uint32_t id)
	{
		for (const auto& name : WEAPON_NAMES) {
			if (name.id == id) {
				return name.classname;
			}
		}
		return nullptr;
	}
}
