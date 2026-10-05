// HalfCraft.exe, the one click. finds half-life 2 in steam (and half-life 2: deathmatch, whose 64-bit
// engine it can run instead of half-life 2's own 32-bit one), starts the bundled minecraft (the first
// time it waits until the player has signed in to it), then the engine on its mod folder beside it.
// extra arguments go to the engine:
//
//   HalfCraft.exe -windowed -w 1280 -h 720 +map d1_canals_01
//
// with both games installed it asks which engine to run and keeps the answer in HalfCraft.ini beside
// it. holding shift while starting it asks again.
//
// it expects the release layout (core/hc_prism.h): game-hl2/, game-hl2dm/ and minecraft/ next to it.

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>

#include "core/hc_prism.h"

namespace
{
	namespace fs = std::filesystem;
	using namespace std::chrono_literals;

	constexpr wchar_t TITLE[] = L"HalfCraft";
	constexpr wchar_t SETTINGS_FILE[] = L"HalfCraft.ini";
	constexpr wchar_t SETTINGS_SECTION[] = L"launcher";
	constexpr wchar_t SETTINGS_ENGINE[] = L"engine";
	constexpr wchar_t STEAM_KEY[] = L"Software\\Valve\\Steam";
	constexpr wchar_t STEAM_PROCESS_KEY[] = L"Software\\Valve\\Steam\\ActiveProcess";
	constexpr auto    STEAM_SIGN_IN_WAIT = 3min;
	constexpr auto    OLD_MINECRAFT_WAIT = 30s;  // the last game's minecraft saves and quits within a few seconds

	struct SteamApp
	{
		int            id;
		const wchar_t* name;
	};
	constexpr SteamApp HALF_LIFE_2{ 220, L"Half-Life 2" };
	constexpr SteamApp DEATHMATCH{ 320, L"Half-Life 2: Deathmatch" };
	// they come with half-life 2 and install into its folder; their chapters need them
	constexpr SteamApp EPISODES[]{ { 380, L"Half-Life 2: Episode One" }, { 420, L"Half-Life 2: Episode Two" } };
	constexpr wchar_t  SETTINGS_EPISODES_DECLINED[] = L"episodes_declined";

	/// a source engine halfcraft runs on. the release has a mod folder for each: game-<id>.
	struct Engine
	{
		const wchar_t* id;      // its name in HalfCraft.ini and in its mod folder's
		SteamApp       app;     // the game it comes with
		const wchar_t* exe;     // in that game's folder
		const wchar_t* choice;  // what the player picks it by
	};
	// half-life 2 is the one game halfcraft always needs (its content), so its engine is always there.
	// deathmatch's is the 64-bit one: more memory, for players who have it
	constexpr Engine ENGINES[]{
		{ L"hl2", HALF_LIFE_2, L"hl2.exe", L"Half-Life 2 (32-bit engine)" },
		{ L"hl2dm", DEATHMATCH, L"hl2mp_win64.exe", L"Half-Life 2: Deathmatch engine (64-bit, more memory)" },
	};

	void tell(const std::wstring& text, UINT icon = MB_ICONINFORMATION) { ::MessageBoxW(nullptr, text.c_str(), TITLE, MB_OK | icon); }

	bool ask(const std::wstring& text) { return ::MessageBoxW(nullptr, text.c_str(), TITLE, MB_YESNO | MB_ICONWARNING) == IDYES; }

	int fail(const std::wstring& text)
	{
		tell(text, MB_ICONERROR);
		return 1;
	}

	std::wstring widen(const std::string& utf8)
	{
		if (utf8.empty()) {
			return {};
		}
		const int    length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
		std::wstring wide(static_cast<std::size_t>(length), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
		return wide;
	}

	std::wstring registry_string(const wchar_t* key, const wchar_t* value)
	{
		wchar_t buffer[MAX_PATH * 2];
		DWORD   size = sizeof(buffer);
		if (::RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
			return {};
		}
		return buffer;
	}

	DWORD registry_dword(const wchar_t* key, const wchar_t* value)
	{
		DWORD result = 0;
		DWORD size = sizeof(result);
		return ::RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_DWORD, nullptr, &result, &size) == ERROR_SUCCESS ? result : 0;
	}

	/// the values of every `"key"  "value"` line with this key in a steam .vdf or .acf file.
	std::vector<std::wstring> vdf_values(const fs::path& file, const std::string& key)
	{
		std::vector<std::wstring> values;
		std::ifstream             in(file);
		const std::string         quoted = "\"" + key + "\"";
		for (std::string line; std::getline(in, line);) {
			const auto at = line.find(quoted);
			const auto open = at == std::string::npos ? std::string::npos : line.find('"', at + quoted.size());
			const auto close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
			if (close == std::string::npos) {
				continue;
			}
			std::string value;
			for (auto i = open + 1; i < close; ++i) {
				if (line[i] == '\\' && i + 1 < close) {
					++i;  // vdf escapes backslashes
				}
				value += line[i];
			}
			values.push_back(widen(value));
		}
		return values;
	}

	std::vector<fs::path> steam_libraries(const fs::path& steam)
	{
		std::vector<fs::path> libraries{ steam };
		for (const auto& path : vdf_values(steam / L"steamapps" / L"libraryfolders.vdf", "path")) {
			libraries.emplace_back(path);
		}
		return libraries;
	}

	/// where steam has an app fully installed, or empty.
	fs::path app_dir(const std::vector<fs::path>& libraries, const SteamApp& app)
	{
		for (const auto& library : libraries) {
			const auto manifest = library / L"steamapps" / (L"appmanifest_" + std::to_wstring(app.id) + L".acf");
			const auto names = vdf_values(manifest, "installdir");
			const auto flags = vdf_values(manifest, "StateFlags");
			if (names.empty() || flags.empty() || !(std::wcstol(flags.front().c_str(), nullptr, 10) & 4)) {
				continue;  // not in this library, or still downloading
			}
			std::error_code ec;
			const auto      dir = library / L"steamapps" / L"common" / names.front();
			if (fs::is_directory(dir, ec)) {
				return dir;
			}
		}
		return {};
	}

	bool process_running(const wchar_t* exe)
	{
		HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE) {
			return false;
		}
		bool            found = false;
		PROCESSENTRY32W entry{ sizeof(entry) };
		for (BOOL more = ::Process32FirstW(snapshot, &entry); more && !found; more = ::Process32NextW(snapshot, &entry)) {
			found = _wcsicmp(entry.szExeFile, exe) == 0;
		}
		::CloseHandle(snapshot);
		return found;
	}

	bool steam_signed_in()
	{
		const DWORD pid = registry_dword(STEAM_PROCESS_KEY, L"pid");
		if (!pid || !registry_dword(STEAM_PROCESS_KEY, L"ActiveUser")) {
			return false;
		}
		HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);  // the values outlive a steam that crashed
		if (!process) {
			return false;
		}
		const bool alive = ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
		::CloseHandle(process);
		return alive;
	}

	/// the engine started without a signed-in steam would leave for steam's own launch of it, without
	/// the mod folder. so steam goes first.
	bool ensure_steam()
	{
		if (steam_signed_in()) {
			return true;
		}
		const std::wstring steam_exe = registry_string(STEAM_KEY, L"SteamExe");
		if (steam_exe.empty() || reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", steam_exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
			fail(L"Couldn't start Steam. Start it, sign in, then start HalfCraft again.");
			return false;
		}
		const auto give_up = std::chrono::steady_clock::now() + STEAM_SIGN_IN_WAIT;
		while (!steam_signed_in()) {
			if (std::chrono::steady_clock::now() > give_up) {
				fail(L"Steam isn't signed in. Sign in, then start HalfCraft again.");
				return false;
			}
			std::this_thread::sleep_for(500ms);
		}
		return true;
	}

	/// minecraft running, or on its way with an account to play with.
	bool ensure_minecraft(const fs::path& game)
	{
		const auto bundle = halfcraft::prism::find_bundle(game);
		if (bundle.empty()) {
			fail(L"The minecraft folder next to HalfCraft.exe is missing. Unpack the whole HalfCraft zip again.");
			return false;
		}
		// one running now is most likely the last game's on its way out: wait for it, then start a new one
		const auto give_up = std::chrono::steady_clock::now() + OLD_MINECRAFT_WAIT;
		while (halfcraft::prism::minecraft_running() && std::chrono::steady_clock::now() < give_up) {
			std::this_thread::sleep_for(500ms);
		}
		if (halfcraft::prism::minecraft_running()) {
			return true;  // it stays: it connects to this game instead
		}
		if (!halfcraft::prism::launcher_running(bundle)) {
			if (!halfcraft::prism::signed_in(bundle)) {
				tell(L"First start: Prism Launcher opens and asks you to sign in with the Microsoft account that owns Minecraft: Java Edition. "
					 L"Then it downloads Minecraft and Java by itself.\n\n"
					 L"Half-Life 2 starts as soon as you're signed in. Minecraft joins it once it's ready (the first time takes a few minutes).");
			}
			std::wstring error;
			if (!halfcraft::prism::start(bundle, error)) {
				fail(L"Couldn't start Minecraft.\n\n" + error);
				return false;
			}
		}
		// prism quits if the player closes it without signing in (or plays its demo, which is fine too)
		while (!halfcraft::prism::signed_in(bundle) && !halfcraft::prism::minecraft_running()) {
			if (!halfcraft::prism::launcher_running(bundle) && !halfcraft::prism::minecraft_running()) {
				tell(L"HalfCraft needs Minecraft, and Minecraft needs an account. Start HalfCraft again to sign in.", MB_ICONWARNING);
				return false;
			}
			std::this_thread::sleep_for(500ms);
		}
		return true;
	}

	/// half-life's file system reads paths in the windows ("ansi") code page.
	bool fits_ansi_code_page(const std::wstring& text)
	{
		BOOL lossy = FALSE;
		return ::WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.c_str(), -1, nullptr, 0, nullptr, &lossy) > 0 && !lossy;
	}

	fs::path exe_dir()
	{
		wchar_t path[MAX_PATH * 2];
		const DWORD length = ::GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
		return fs::path(std::wstring(path, length)).parent_path();
	}

	fs::path game_dir(const fs::path& here, const Engine& engine) { return here / (std::wstring(L"game-") + engine.id); }

	bool is_game_dir(const fs::path& dir)
	{
		std::error_code ec;
		return fs::exists(dir / L"gameinfo.txt", ec);
	}

	/// the engines whose game steam has installed, in ENGINES' order.
	std::vector<const Engine*> installed_engines(const std::vector<fs::path>& libraries)
	{
		std::vector<const Engine*> installed;
		for (const auto& engine : ENGINES) {
			if (!app_dir(libraries, engine.app).empty()) {
				installed.push_back(&engine);
			}
		}
		return installed;
	}

	/// the engine HalfCraft.ini says to run without asking, or nullptr.
	const Engine* remembered_engine(const fs::path& settings)
	{
		wchar_t id[32];
		::GetPrivateProfileStringW(SETTINGS_SECTION, SETTINGS_ENGINE, L"", id, static_cast<DWORD>(std::size(id)), settings.c_str());
		const auto found = std::find_if(std::begin(ENGINES), std::end(ENGINES), [&](const Engine& engine) { return _wcsicmp(id, engine.id) == 0; });
		return found == std::end(ENGINES) ? nullptr : found;
	}

	/// asks the player which engine to run.
	/// @param engines - the installed ones to pick from
	/// @param remembered - the last choice, preselected (or nullptr)
	/// @param remember - set to whether the player wants this choice kept
	/// @return the choice, or nullptr when the player closed the question or it couldn't be asked
	const Engine* ask_engine(const std::vector<const Engine*>& engines, const Engine* remembered, bool& remember)
	{
		constexpr int FIRST_BUTTON = 100;  // clear of the common buttons' ids (IDCANCEL, ...)

		TASKDIALOGCONFIG               config{ sizeof(config) };
		std::vector<TASKDIALOG_BUTTON> buttons;
		for (const Engine* engine : engines) {
			const int id = FIRST_BUTTON + static_cast<int>(buttons.size());
			buttons.push_back({ id, engine->choice });
			if (engine == remembered) {
				config.nDefaultButton = id;
			}
		}
		config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_VERIFICATION_FLAG_CHECKED;
		config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
		config.pszWindowTitle = TITLE;
		config.pszMainInstruction = L"Which engine should run HalfCraft?";
		config.pszContent = L"Both play the same HalfCraft. Half-Life 2: Deathmatch's engine is 64-bit, so it can use more memory. "
							L"Each engine keeps its own saves.";
		config.cButtons = static_cast<UINT>(buttons.size());
		config.pButtons = buttons.data();
		config.pszVerificationText = L"Remember my choice (hold Shift while starting HalfCraft to choose again)";

		int           button = 0;
		BOOL          checked = FALSE;
		const HRESULT result = ::TaskDialogIndirect(&config, &button, nullptr, &checked);
		if (FAILED(result)) {
			wchar_t code[16];
			std::swprintf(code, std::size(code), L"0x%08lX", static_cast<unsigned long>(result));
			fail(L"Couldn't ask which engine to run (error " + std::wstring(code) + L").");
			return nullptr;
		}
		remember = checked != FALSE;
		const int picked = button - FIRST_BUTTON;
		return picked >= 0 && picked < static_cast<int>(engines.size()) ? engines[static_cast<std::size_t>(picked)] : nullptr;
	}

	/// the engine to run: the only one installed, the one the player chose to keep, or a new choice
	/// (kept in HalfCraft.ini if the player wants).
	/// @param installed - the installed engines, at least one
	/// @param settings - HalfCraft.ini
	/// @param ask_again - the player held shift to choose again
	/// @return nullptr when the player didn't choose
	const Engine* choose_engine(const std::vector<const Engine*>& installed, const fs::path& settings, bool ask_again)
	{
		if (installed.size() < 2) {
			// nothing to choose. a remembered choice stays for when the other game is back
			return installed.empty() ? nullptr : installed.front();
		}
		const Engine* remembered = remembered_engine(settings);
		if (!ask_again && std::find(installed.begin(), installed.end(), remembered) != installed.end()) {
			return remembered;
		}
		bool          remember = true;
		const Engine* chosen = ask_engine(installed, remembered, remember);
		if (!chosen || (!remember && !remembered)) {
			return chosen;
		}
		// a choice not to remember drops the old one too, so the next start asks again
		if (!::WritePrivateProfileStringW(SETTINGS_SECTION, SETTINGS_ENGINE, remember ? chosen->id : nullptr, settings.c_str())) {
			const DWORD error = ::GetLastError();
			tell(L"Couldn't save your choice in " + settings.wstring() + L" (error " + std::to_wstring(error) + L"), so HalfCraft asks again next time.",
				 MB_ICONWARNING);
		}
		return chosen;
	}

	/// offers steam's install of the episodes it hasn't installed: half-life 2's chapters play without them,
	/// theirs don't. a player who says no isn't asked again (HalfCraft.ini).
	void offer_episodes(const std::vector<fs::path>& libraries, const fs::path& settings)
	{
		std::wstring     names;
		std::vector<int> missing;
		for (const auto& episode : EPISODES) {
			if (app_dir(libraries, episode).empty()) {
				names += L"\n" + std::wstring(episode.name);
				missing.push_back(episode.id);
			}
		}
		if (missing.empty() || ::GetPrivateProfileIntW(SETTINGS_SECTION, SETTINGS_EPISODES_DECLINED, 0, settings.c_str()) != 0) {
			return;
		}
		if (ask(L"Steam hasn't installed:" + names + L"\n\nThey come with Half-Life 2. Their chapters won't load without them; "
				L"Half-Life 2's will.\n\nOpen Steam to install them?")) {
			for (const int id : missing) {
				::ShellExecuteW(nullptr, L"open", (L"steam://install/" + std::to_wstring(id)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			}
		} else {
			::WritePrivateProfileStringW(SETTINGS_SECTION, SETTINGS_EPISODES_DECLINED, L"1", settings.c_str());
		}
	}

	int run()
	{
		// read first thing: the player lets go of shift once HalfCraft has started
		const bool shift_held = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

		const fs::path here = exe_dir();
		if (std::none_of(std::begin(ENGINES), std::end(ENGINES), [&](const Engine& engine) { return is_game_dir(game_dir(here, engine)); })) {
			return fail(L"HalfCraft.exe has to stay in the HalfCraft folder, next to its game-hl2, game-hl2dm and minecraft folders.");
		}
		if (!fits_ansi_code_page(here.wstring()) &&
			!ask(L"HalfCraft's folder has characters in its path that Half-Life 2 may not read:\n" + here.wstring() +
				 L"\n\nIf the game doesn't start, move the HalfCraft folder somewhere with a plain English path, like C:\\Games\\HalfCraft.\n\nStart anyway?")) {
			return 1;
		}

		const std::wstring steam = registry_string(STEAM_KEY, L"SteamPath");
		if (steam.empty()) {
			return fail(L"Steam isn't installed. HalfCraft needs Half-Life 2 from Steam.");
		}
		const auto libraries = steam_libraries(steam);
		if (app_dir(libraries, HALF_LIFE_2).empty()) {
			if (ask(std::wstring(HALF_LIFE_2.name) + L" isn't installed. HalfCraft needs it from Steam.\n\nOpen Steam to install it?")) {
				::ShellExecuteW(nullptr, L"open", (L"steam://install/" + std::to_wstring(HALF_LIFE_2.id)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			}
			return 1;
		}
		offer_episodes(libraries, here / SETTINGS_FILE);
		// source allows one game at a time, whichever engine runs it
		for (const auto& running : ENGINES) {
			if (process_running(running.exe)) {
				return fail(std::wstring(running.app.name) + L" (" + running.exe +
							L") is already running, and only one Source game can run at a time. Close it, then start HalfCraft again.");
			}
		}

		const Engine* engine = choose_engine(installed_engines(libraries), here / SETTINGS_FILE, shift_held);
		if (!engine) {
			return 1;
		}
		const fs::path game = game_dir(here, *engine);
		if (!is_game_dir(game)) {
			return fail(L"The " + game.filename().wstring() + L" folder next to HalfCraft.exe is missing. Unpack the whole HalfCraft zip again.");
		}
		const fs::path  exe = app_dir(libraries, engine->app) / engine->exe;
		std::error_code ec;
		if (!fs::exists(exe, ec)) {
			return fail(std::wstring(engine->app.name) + L" has no " + engine->exe +
						L". Let Steam update it, or check its files (Properties, Installed Files, Verify integrity).");
		}

		if (!ensure_steam() || !ensure_minecraft(game)) {
			return 1;
		}

		std::wstring   command = L"\"" + exe.wstring() + L"\" -game \"" + game.wstring() + L"\" -novid -condebug";
		const wchar_t* extra = ::PathGetArgsW(::GetCommandLineW());
		if (extra && *extra) {
			command += L" ";
			command += extra;
		}
		STARTUPINFOW        startup{ sizeof(startup) };
		PROCESS_INFORMATION process{};
		if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &startup, &process)) {
			return fail(L"Couldn't start " + exe.wstring() + L" (error " + std::to_wstring(::GetLastError()) + L").");
		}
		::CloseHandle(process.hThread);
		::CloseHandle(process.hProcess);
		return 0;
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	// a second click while the first one waits (for steam, or the first sign-in) does nothing
	HANDLE single = ::CreateMutexW(nullptr, TRUE, L"Local\\HalfCraft_launcher");
	if (single && ::GetLastError() == ERROR_ALREADY_EXISTS) {
		::CloseHandle(single);
		return 0;
	}
	const int result = run();
	if (single) {
		::CloseHandle(single);
	}
	return result;
}
