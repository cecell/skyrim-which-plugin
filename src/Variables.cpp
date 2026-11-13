#include "Variables.h"

#include "ConsoleCommands.h"
#include "Redirect.h"
#include "Settings.h"

#include <fstream>

#include <Windows.h>
#include <nlohmann/json.hpp>

namespace Variables
{
	namespace
	{
		using ConsoleCommands::Print;

		std::map<std::string, std::string>& User()
		{
			static std::map<std::string, std::string> vars;
			return vars;
		}

		std::string g_lastOutput;
		RE::FormID  g_lastRecord = 0;
		bool        g_loaded = false;

		constexpr std::array kBuiltIns{ "SELECTED"sv, "CROSSHAIR"sv, "PLAYER"sv, "CELL"sv, "WORLDSPACE"sv,
			"LAST_RECORD"sv, "LAST_FILE"sv, "LAST_OUTPUT"sv, "CLIPBOARD"sv };

		std::string Upper(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return out;
		}

		std::string_view Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t");
			if (first == std::string_view::npos) {
				return {};
			}
			return a_text.substr(first, a_text.find_last_not_of(" \t") - first + 1);
		}

		bool IsNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
		bool IsNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

		bool IsBuiltIn(std::string_view a_name)
		{
			return std::ranges::find(kBuiltIns, Upper(a_name)) != kBuiltIns.end();
		}

		std::optional<std::filesystem::path> File()
		{
			const auto folder = Redirect::OutputFolder();
			return folder ? std::optional{ *folder / "vars.json" } : std::nullopt;
		}

		void Load()
		{
			if (g_loaded) {
				return;
			}
			g_loaded = true;
			const auto path = File();
			std::error_code ec;
			if (!path || !std::filesystem::exists(*path, ec)) {
				return;
			}
			try {
				std::ifstream in(*path, std::ios::binary);
				const auto    json = nlohmann::json::parse(in);
				for (const auto& [name, value] : json.items()) {
					if (value.is_string() && !name.empty() && IsNameStart(name[0]) && std::ranges::all_of(name, IsNameChar) && !IsBuiltIn(name)) {
						User()[name] = value.get<std::string>();
					}
				}
			} catch (const std::exception& e) {
				SKSE::log::warn("{} couldn't be read, so no saved variables were loaded: {}", path->string(), e.what());
				Print(std::format("  Warning: {} couldn't be read ({}); saved variables weren't loaded", path->string(), e.what()));
			}
		}

		bool Save()
		{
			const auto path = File();
			if (!path) {
				Print("  Couldn't find the My Games\\Skyrim Special Edition folder; the variable is set for this session only");
				return false;
			}
			nlohmann::json json = nlohmann::json::object();
			for (const auto& [name, value] : User()) {
				json[name] = value;
			}
			std::error_code ec;
			std::filesystem::create_directories(path->parent_path(), ec);
			std::ofstream out(*path, std::ios::binary | std::ios::trunc);
			out << json.dump(2) << '\n';
			if (!out) {
				Print(std::format("  Couldn't write {}; the variable is set for this session only", path->string()));
				return false;
			}
			return true;
		}

		std::string Hex(RE::FormID a_id) { return std::format("{:08X}", a_id); }

		std::string ReadClipboard(std::string& a_error)
		{
			if (!OpenClipboard(nullptr)) {
				a_error = "the clipboard is in use by another program";
				return {};
			}
			std::string text;
			if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
				if (const auto* wide = static_cast<const wchar_t*>(GlobalLock(data))) {
					// Game strings are in the Windows ANSI code page.
					const int size = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
					if (size > 1) {
						text.resize(static_cast<std::size_t>(size - 1));
						WideCharToMultiByte(CP_ACP, 0, wide, -1, text.data(), size, nullptr, nullptr);
					}
					GlobalUnlock(data);
				}
			}
			CloseClipboard();
			if (text.empty()) {
				a_error = "the clipboard has no text";
			}
			// One line, like $LAST_OUTPUT: line breaks become spaces.
			std::ranges::replace(text, '\r', ' ');
			std::ranges::replace(text, '\n', ' ');
			return std::string{ Trim(text) };
		}

		// A built-in's current value, or nullopt with a_error saying why it has none.
		std::optional<std::string> BuiltIn(std::string_view a_name, std::string& a_error)
		{
			const auto name = Upper(a_name);
			auto*      player = RE::PlayerCharacter::GetSingleton();
			if (name == "PLAYER") {
				return Hex(0x14);
			}
			if (name == "SELECTED") {
				if (const auto ref = RE::Console::GetSelectedRef()) {
					return Hex(ref->GetFormID());
				}
				a_error = "no reference is selected in the console (click one with the console open)";
			} else if (name == "CROSSHAIR") {
				const auto* pick = RE::CrosshairPickData::GetSingleton();
				if (const auto ref = pick ? pick->GetActiveTarget().get() : nullptr) {
					return Hex(ref->GetFormID());
				}
				a_error = "nothing was under the crosshair when the console opened";
			} else if (name == "CELL") {
				if (const auto* cell = player ? player->GetParentCell() : nullptr) {
					return Hex(cell->GetFormID());
				}
				a_error = "the player isn't in a cell (no game loaded?)";
			} else if (name == "WORLDSPACE") {
				if (const auto* world = player ? player->GetWorldspace() : nullptr) {
					return Hex(world->GetFormID());
				}
				a_error = "the player is in an interior, which has no worldspace";
			} else if (name == "LAST_RECORD") {
				if (g_lastRecord) {
					return Hex(g_lastRecord);
				}
				a_error = "no record has been shown in detail yet (e.g. wp 00012EB7)";
			} else if (name == "LAST_FILE") {
				if (!Redirect::LastFile().empty()) {
					return Redirect::LastFile().string();
				}
				a_error = "no file has been written yet this session";
			} else if (name == "LAST_OUTPUT") {
				return g_lastOutput;
			} else if (name == "CLIPBOARD") {
				if (!Settings::Get().allowClipboardRead) {
					a_error = "reading the clipboard is off (bAllowClipboardRead in WhichPlugin.ini)";
				} else if (auto text = ReadClipboard(a_error); a_error.empty()) {
					return text;
				}
			}
			return std::nullopt;
		}

		std::optional<std::string> Value(std::string_view a_name, std::string& a_error)
		{
			if (IsBuiltIn(a_name)) {
				return BuiltIn(a_name, a_error);
			}
			Load();
			if (const auto it = User().find(std::string{ a_name }); it != User().end()) {
				return it->second;
			}
			a_error = std::format("${} isn't set (wp env lists the variables)", a_name);
			return std::nullopt;
		}

		// The end of a $( ... ) starting at a_open (the '('), skipping quotes and nested parentheses.
		std::size_t MatchingParen(std::string_view a_text, std::size_t a_open)
		{
			int  depth = 0;
			char quote = 0;
			for (std::size_t i = a_open; i < a_text.size(); ++i) {
				const char c = a_text[i];
				if (quote) {
					if (quote == '\'' && c == '\\' && i + 1 < a_text.size() && a_text[i + 1] == '\'') {
						++i;
					} else if (c == quote) {
						quote = 0;
					}
				} else if (c == '"' || c == '\'') {
					quote = c;
				} else if (c == '(') {
					++depth;
				} else if (c == ')' && --depth == 0) {
					return i;
				}
			}
			return std::string_view::npos;
		}

		std::string Shorten(const std::string& a_value)
		{
			return a_value.size() <= 80 ? a_value : std::format("{}... ({} characters)", a_value.substr(0, 77), a_value.size());
		}
	}

	bool Expand(std::string_view a_text, const Runner& a_run, std::string& a_out, std::string& a_error)
	{
		static int depth = 0;  // $(...) inside $(...)
		a_out.clear();
		bool inSingle = false;
		bool inDouble = false;
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			const char c = a_text[i];
			if (inSingle) {
				a_out += c;
				if (c == '\\' && i + 1 < a_text.size() && a_text[i + 1] == '\'') {
					a_out += a_text[++i];
				} else if (c == '\'') {
					inSingle = false;
				}
				continue;
			}
			if (c == '\'' && !inDouble) {
				inSingle = true;
				a_out += c;
				continue;
			}
			if (c == '"') {
				inDouble = !inDouble;
				a_out += c;
				continue;
			}
			if (c == '\\' && i + 1 < a_text.size() && a_text[i + 1] == '$') {
				a_out += '$';
				++i;
				continue;
			}
			if (c != '$' || i + 1 >= a_text.size()) {
				a_out += c;
				continue;
			}

			const char next = a_text[i + 1];
			if (next == '(') {
				const auto close = MatchingParen(a_text, i + 1);
				if (close == std::string_view::npos) {
					a_error = "$( has no closing )";
					return false;
				}
				if (depth >= 4) {
					a_error = "$(...) is nested too deeply";
					return false;
				}
				++depth;
				const auto lines = a_run(Trim(a_text.substr(i + 2, close - i - 2)), a_error);
				--depth;
				if (!lines) {
					return false;
				}
				std::string joined;
				for (const auto& line : *lines) {
					if (const auto t = Trim(line); !t.empty()) {
						joined += joined.empty() ? "" : " ";
						joined += t;
					}
				}
				a_out += joined;
				i = close;
				continue;
			}

			std::string_view name;
			std::size_t      end = 0;
			if (next == '{') {
				end = a_text.find('}', i + 2);
				if (end == std::string_view::npos) {
					a_error = "${ has no closing }";
					return false;
				}
				name = a_text.substr(i + 2, end - i - 2);
				if (name.empty() || !IsNameStart(name[0]) || !std::ranges::all_of(name, IsNameChar)) {
					a_error = std::format("${{{}}} isn't a variable name (letters, digits and _)", name);
					return false;
				}
			} else if (IsNameStart(next)) {
				end = i + 1;
				while (end + 1 < a_text.size() && IsNameChar(a_text[end + 1])) {
					++end;
				}
				name = a_text.substr(i + 1, end - i);
			} else {
				a_out += c;  // a plain $, e.g. the end of a regex
				continue;
			}
			const auto value = Value(name, a_error);
			if (!value) {
				if (a_error.empty()) {
					a_error = std::format("${} has no value", name);
				}
				return false;
			}
			a_out += *value;
			i = end;
		}
		return true;
	}

	bool IsCommand(std::string_view a_args)
	{
		const auto text = Trim(a_args);
		const auto word = Upper(text.substr(0, text.find_first_of(" \t")));
		return word == "EXPORT" || word == "UNSET" || word == "ENV";
	}

	bool RunCommand(std::string_view a_args)
	{
		if (!IsCommand(a_args)) {
			return false;
		}
		const auto text = Trim(a_args);
		const auto split = text.find_first_of(" \t");
		const auto word = Upper(text.substr(0, split));
		const auto rest = split == std::string_view::npos ? std::string_view{} : Trim(text.substr(split));
		Load();

		if (word == "ENV") {
			if (!rest.empty()) {
				Print("  wp env takes nothing after it");
				return true;
			}
			Print(User().empty() ? "  No variables set. Set one with: wp export NAME=value" : std::format("  Variables ({}), saved in wp\\vars.json:", User().size()));
			for (const auto& [name, value] : User()) {
				Print(std::format("    {}={}", name, Shorten(value)));
			}
			Print("  Built-in:");
			for (const auto name : kBuiltIns) {
				std::string error;
				const auto  value = name == "CLIPBOARD" ? std::optional<std::string>{} : BuiltIn(name, error);
				Print(std::format("    {}={}", name, name == "CLIPBOARD" ? "(the clipboard's text when used)" : value ? Shorten(*value) : std::format("(none: {})", error)));
			}
			return true;
		}

		const auto valid = [](std::string_view a_name) {
			return !a_name.empty() && IsNameStart(a_name[0]) && std::ranges::all_of(a_name, IsNameChar);
		};

		if (word == "UNSET") {
			if (!valid(rest)) {
				Print("  wp unset NAME: removes a variable you set (wp env lists them)");
			} else if (User().erase(std::string{ rest }) == 0) {
				Print(std::format("  ${} isn't set", rest));
			} else {
				Save();
				Print(std::format("  Removed ${}", rest));
			}
			return true;
		}

		// export NAME=value
		const auto equals = rest.find('=');
		const auto name = equals == std::string_view::npos ? rest : Trim(rest.substr(0, equals));
		if (equals == std::string_view::npos || !valid(name)) {
			Print("  wp export NAME=value: NAME is letters, digits and _ (not starting with a digit)");
			return true;
		}
		if (IsBuiltIn(name)) {
			Print(std::format("  ${} is built in and can't be set", Upper(name)));
			return true;
		}
		std::string value{ Trim(rest.substr(equals + 1)) };
		if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
			value = value.substr(1, value.size() - 2);
		}
		const auto& s = Settings::Get();
		if (value.size() > s.maxVariableLength) {
			Print(std::format("  ${} would be {} characters; the limit is {} (iMaxVariableLength)", name, value.size(), s.maxVariableLength));
			return true;
		}
		if (!User().contains(std::string{ name }) && User().size() >= s.maxVariables) {
			Print(std::format("  There are already {} variables, the limit (iMaxVariables); wp unset one first", User().size()));
			return true;
		}
		User()[std::string{ name }] = value;
		Save();
		Print(std::format("  ${}={}", name, Shorten(value)));
		return true;
	}

	void SetLastOutput(const std::vector<std::string>& a_lines)
	{
		g_lastOutput.clear();
		for (const auto& line : a_lines) {
			if (const auto t = Trim(line); !t.empty()) {
				g_lastOutput += g_lastOutput.empty() ? "" : " ";
				g_lastOutput += t;
			}
		}
	}

	void SetLastRecord(RE::FormID a_id)
	{
		g_lastRecord = a_id;
	}
}
