#pragma once

// the minecraft a halfcraft release ships: a portable prism launcher with a ready "HalfCraft"
// instance, in the release's minecraft folder beside the half-life mod folder (game). HalfCraft.exe
// and client.dll both start it from there. engine-agnostic (win32 only).
//
//   HalfCraft/
//     HalfCraft.exe
//     game/                       the mod folder hl2mp_win64.exe runs (-game)
//     minecraft/
//       Prism/prismlauncher.exe   portable prism: the account, minecraft, java and the world live here
//       defaults/                 prism's settings for its first start

#include <filesystem>
#include <string>

namespace halfcraft::prism
{
	/// the bundled minecraft beside a mod folder.
	/// @param game_dir - the mod folder (<release>/game)
	/// @return <release>/minecraft, or empty when there's no bundled prism (a development checkout)
	std::filesystem::path find_bundle(const std::filesystem::path& game_dir);

	/// a minecraft with the halfcraft mod is running (it holds a named mutex for as long as it runs).
	bool minecraft_running();

	/// the bundle's own prism is running: downloading, asking for an account, or minding the game.
	bool launcher_running(const std::filesystem::path& bundle);

	/// prism has an account to play with.
	bool signed_in(const std::filesystem::path& bundle);

	/// starts the bundle's "HalfCraft" instance through prism, which signs in and downloads minecraft
	/// and java itself the first time. that first start also puts prism's default settings in place.
	/// @param error - why it couldn't start, when it couldn't
	/// @return false when prism couldn't be started
	bool start(const std::filesystem::path& bundle, std::wstring& error);
}
