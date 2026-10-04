// HalfCraft.exe, the one click. finds half-life 2 and half-life 2: deathmatch in steam, starts the
// bundled minecraft (the first time it waits until the player has signed in to it), then half-life
// 2: deathmatch's engine on the mod folder beside it. extra arguments go to the engine:
//
//   HalfCraft.exe -windowed -w 1280 -h 720 +map d1_canals_01
//
// it expects the release layout (core/hc_prism.h): game/ and minecraft/ next to it.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>

#include "core/hc_prism.h"

namespace
{
	namespace fs = std::filesystem;
	using namespace std::chrono_literals;

	constexpr wchar_t TITLE[] = L"HalfCraft";
	constexpr wchar_t ENGINE_EXE[] = L"hl2mp_win64.exe";
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

	int run()
	{
		const fs::path here = exe_dir();
		const fs::path game = here / L"game";
		std::error_code ec;
		if (!fs::exists(game / L"gameinfo.txt", ec)) {
			return fail(L"HalfCraft.exe has to stay in the HalfCraft folder, next to its game and minecraft folders.");
		}
		if (!fits_ansi_code_page(game.wstring()) &&
			!ask(L"HalfCraft's folder has characters in its path that Half-Life 2 may not read:\n" + here.wstring() +
				 L"\n\nIf the game doesn't start, move the HalfCraft folder somewhere with a plain English path, like C:\\Games\\HalfCraft.\n\nStart anyway?")) {
			return 1;
		}

		const std::wstring steam = registry_string(STEAM_KEY, L"SteamPath");
		if (steam.empty()) {
			return fail(L"Steam isn't installed. HalfCraft needs Half-Life 2 and Half-Life 2: Deathmatch from Steam.");
		}
		const auto libraries = steam_libraries(steam);
		for (const auto& app : { HALF_LIFE_2, DEATHMATCH }) {
			if (app_dir(libraries, app).empty()) {
				if (ask(std::wstring(app.name) + L" isn't installed. HalfCraft needs it from Steam.\n\nOpen Steam to install it?")) {
					::ShellExecuteW(nullptr, L"open", (L"steam://install/" + std::to_wstring(app.id)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				return 1;
			}
		}
		const fs::path engine = app_dir(libraries, DEATHMATCH) / ENGINE_EXE;
		if (!fs::exists(engine, ec)) {
			return fail(L"Half-Life 2: Deathmatch has no " + std::wstring(ENGINE_EXE) +
						L". Let Steam update it, or check its files (Properties, Installed Files, Verify integrity).");
		}
		if (process_running(ENGINE_EXE)) {
			return fail(L"Half-Life 2: Deathmatch is already running. Close it, then start HalfCraft again.");
		}

		if (!ensure_steam() || !ensure_minecraft(game)) {
			return 1;
		}

		std::wstring   command = L"\"" + engine.wstring() + L"\" -game \"" + game.wstring() + L"\" -novid -condebug";
		const wchar_t* extra = ::PathGetArgsW(::GetCommandLineW());
		if (extra && *extra) {
			command += L" ";
			command += extra;
		}
		STARTUPINFOW        startup{ sizeof(startup) };
		PROCESS_INFORMATION process{};
		if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, engine.parent_path().c_str(), &startup, &process)) {
			return fail(L"Couldn't start " + engine.wstring() + L" (error " + std::to_wstring(::GetLastError()) + L").");
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
