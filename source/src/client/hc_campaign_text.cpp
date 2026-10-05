// client.dll: titles.txt and credits.txt of the campaign being played. half-life 2 and its episodes each
// have their own under the same names, and one game mounts only one of each: the engine keeps the first
// titles.txt it finds (episode two's, which lacks the other campaigns' chapter titles) and env_credits
// reads the game folder's credits.txt (its logo says "episode two"). gameinfo.txt mounts every
// campaign's content once more under its own search path id (core/hc_campaign.h); this reads from there.

#include "cbase.h"
#include "client_textmessage.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"
#include "tier3/tier3.h"
#include "vgui/ILocalize.h"

#include "tier0/valve_minmax_off.h"
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/hc_campaign.h"
#include "core/hc_log.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr char TITLES_FILE[] = "scripts/titles.txt";

		/// one message of a titles.txt and the strings its client_textmessage_t points at.
		struct Title
		{
			client_textmessage_t message{};
			std::string          name;
			std::string          text;
			std::string          font;
			std::string          clear;
		};

		using Titles = std::vector<std::unique_ptr<Title>>;

		std::string trimmed(const std::string& line)
		{
			const auto first = line.find_first_not_of(" \t\r");
			if (first == std::string::npos) {
				return {};
			}
			return line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
		}

		/// the numbers after a directive's name.
		std::vector<float> numbers(const std::string& line)
		{
			std::vector<float> values;
			const char*        at = line.c_str();
			while (*at && *at != ' ' && *at != '\t') {
				++at;
			}
			char* end = nullptr;
			for (float value = std::strtof(at, &end); end != at; value = std::strtof(at, &end)) {
				values.push_back(value);
				at = end;
			}
			return values;
		}

		/// a "#token" text in the player's language, as the engine reads its titles.txt; anything else as is.
		/// the campaign's own value comes first where the campaigns differ: tools/engines.ps1 writes it as
		/// "<its path id>_<token>"
		std::string localized(const std::string& text, const char* path_id)
		{
			if (text.empty() || text[0] != '#' || !g_pVGuiLocalize) {
				return text;
			}
			const wchar_t* wide = g_pVGuiLocalize->Find(("#" + std::string(path_id) + "_" + text.substr(1)).c_str());
			if (!wide) {
				wide = g_pVGuiLocalize->Find(text.c_str());
			}
			if (!wide) {
				return text;
			}
			char utf8[1024];
			g_pVGuiLocalize->ConvertUnicodeToANSI(wide, utf8, sizeof(utf8));
			return utf8;
		}

		void set_color(const std::vector<float>& rgba, byte& r, byte& g, byte& b, byte& a)
		{
			if (rgba.size() >= 3) {
				r = static_cast<byte>(rgba[0]), g = static_cast<byte>(rgba[1]), b = static_cast<byte>(rgba[2]);
				a = rgba.size() >= 4 ? static_cast<byte>(rgba[3]) : 255;
			}
		}

		/// a titles.txt the way the engine reads one: "$name values" lines set what the messages after
		/// them get; a message is its name, then its text between a "{" and a "}" line; "//" comments.
		/// @param path_id - the campaign's search path id, for its own strings (localized)
		Titles parse_titles(const char* file, const char* path_id)
		{
			Titles                   titles;
			client_textmessage_t     defaults{};
			std::string              font, clear, name;
			std::vector<std::string> body;
			bool                     in_body = false;
			defaults.r1 = defaults.g1 = defaults.b1 = defaults.a1 = 255;
			defaults.x = defaults.y = -1.0f;

			std::string rest = file;
			for (std::size_t start = 0; start <= rest.size();) {
				std::size_t end = rest.find('\n', start);
				if (end == std::string::npos) {
					end = rest.size();
				}
				const std::string line = trimmed(rest.substr(start, end - start));
				start = end + 1;
				if (in_body) {
					if (line != "}") {
						body.push_back(line);
						continue;
					}
					auto title = std::make_unique<Title>();
					title->message = defaults;
					title->name = name;
					for (std::size_t i = 0; i < body.size(); ++i) {
						title->text += (i ? "\n" : "") + body[i];
					}
					title->text = localized(title->text, path_id);
					title->font = font;
					title->clear = clear;
					title->message.pName = title->name.c_str();
					title->message.pMessage = title->text.c_str();
					title->message.pVGuiSchemeFontName = title->font.empty() ? nullptr : title->font.c_str();
					title->message.pClearMessage = title->clear.empty() ? nullptr : title->clear.c_str();
					titles.push_back(std::move(title));
					in_body = false;
					body.clear();
					continue;
				}
				if (line.empty() || line.rfind("//", 0) == 0) {
					continue;
				}
				if (line == "{") {
					in_body = true;
					continue;
				}
				if (line[0] != '$') {
					name = line;
					continue;
				}
				const auto values = numbers(line);
				const auto is = [&line](const char* directive) { return V_strnicmp(line.c_str(), directive, V_strlen(directive)) == 0; };
				if (is("$position") && values.size() >= 2) {
					defaults.x = values[0], defaults.y = values[1];
				} else if (is("$effect") && !values.empty()) {
					defaults.effect = static_cast<int>(values[0]);
				} else if (is("$color2")) {
					set_color(values, defaults.r2, defaults.g2, defaults.b2, defaults.a2);
				} else if (is("$color")) {
					set_color(values, defaults.r1, defaults.g1, defaults.b1, defaults.a1);
				} else if (is("$fadein") && !values.empty()) {
					defaults.fadein = values[0];
				} else if (is("$fadeout") && !values.empty()) {
					defaults.fadeout = values[0];
				} else if (is("$holdtime") && !values.empty()) {
					defaults.holdtime = values[0];
				} else if (is("$fxtime") && !values.empty()) {
					defaults.fxtime = values[0];
				} else if (is("$boxsize") && !values.empty()) {
					defaults.flBoxSize = values[0];
					defaults.bRoundedRectBackdropBox = values[0] != 0.0f;
				} else if (is("$boxcolor") && values.size() >= 4) {
					for (int i = 0; i < 4; ++i) {
						defaults.boxcolor[i] = static_cast<byte>(values[i]);
					}
				} else if (is("$font")) {
					font = trimmed(line.substr(5));
				} else if (is("$clearmessage")) {
					clear = trimmed(line.substr(13));
				}
			}
			return titles;
		}

		Campaign current_campaign()
		{
			return campaign_of(engine->GetLevelName());
		}

		/// the campaign's titles.txt, read once.
		const Titles& campaign_titles(Campaign campaign)
		{
			static std::array<std::unique_ptr<Titles>, 3> loaded;
			auto& titles = loaded[static_cast<std::size_t>(campaign)];
			if (!titles) {
				titles = std::make_unique<Titles>();
				CUtlBuffer file(0, 0, CUtlBuffer::TEXT_BUFFER);
				if (filesystem->ReadFile(TITLES_FILE, campaign_path_id(campaign), file)) {
					file.PutChar('\0');
					*titles = parse_titles(static_cast<const char*>(file.Base()), campaign_path_id(campaign));
					log_info("%s: %d messages from %s", TITLES_FILE, static_cast<int>(titles->size()), campaign_path_id(campaign));
				} else {
					log_warning("%s: none under %s (gameinfo.txt)", TITLES_FILE, campaign_path_id(campaign));
				}
			}
			return *titles;
		}
	}

	client_textmessage_t* client_text_message(const char* name)
	{
		if (!name) {
			return nullptr;
		}
		for (const auto& title : campaign_titles(current_campaign())) {
			if (V_stricmp(title->name.c_str(), name) == 0) {
				return &title->message;
			}
		}
		return nullptr;
	}

	const char* client_campaign_path_id()
	{
		return campaign_path_id(current_campaign());
	}
}
