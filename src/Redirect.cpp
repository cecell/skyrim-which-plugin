#include "Redirect.h"

#include "ConsoleCommands.h"
#include "Settings.h"

#include <fstream>

namespace Redirect
{
	namespace
	{
		std::filesystem::path& Last()
		{
			static std::filesystem::path last;
			return last;
		}

		std::string_view Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t");
			if (first == std::string_view::npos) {
				return {};
			}
			return a_text.substr(first, a_text.find_last_not_of(" \t") - first + 1);
		}

		// Validates a user-typed file name and makes it absolute. Returns an error message or "".
		std::string Resolve(std::string_view a_name, std::filesystem::path& a_out)
		{
			const bool  anywhere = Settings::Get().writeFileAnywhere;
			std::string name{ Trim(a_name) };
			if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front()) {
				name = name.substr(1, name.size() - 2);
			}
			if (name.empty()) {
				return "needs a file name";
			}
			for (std::size_t i = 0; i < name.size(); ++i) {
				const char c = name[i];
				const bool driveColon = anywhere && c == ':' && i == 1;  // D:\...
				if (!driveColon && std::string_view{ "<>:\"|?*" }.find(c) != std::string_view::npos) {
					return std::format("'{}' can't be used in a file name", c);
				}
			}
			std::filesystem::path rel{ name };
			if (!anywhere) {
				if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) {
					return "use a name relative to My Games\\Skyrim Special Edition\\wp, not a full path (bWriteFileAnywhere allows them)";
				}
				for (const auto& part : rel) {
					if (part == "..") {
						return "'..' isn't allowed; files stay inside My Games\\Skyrim Special Edition\\wp (bWriteFileAnywhere allows it)";
					}
				}
			}
			if (!rel.has_extension()) {
				rel += ".txt";
			}
			if (!AllowedExtension(rel)) {
				return std::format("can't write a {} file; use .txt .csv .tsv .log .md .ini .toml .xml .yaml .yml or .json", rel.extension().string());
			}
			const auto base = OutputFolder();
			if (!base) {
				return "couldn't find the My Games\\Skyrim Special Edition folder";
			}
			a_out = rel.is_absolute() ? rel : *base / rel;
			return "";
		}

		bool ToClipboard(const std::string& a_text, std::string& a_error)
		{
			// Game strings (plugin names etc.) are in the Windows ANSI code page.
			const int wideLen = MultiByteToWideChar(CP_ACP, 0, a_text.c_str(), -1, nullptr, 0);
			HGLOBAL   memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wideLen) * sizeof(wchar_t));
			if (!memory) {
				a_error = "out of memory";
				return false;
			}
			auto* wide = static_cast<wchar_t*>(GlobalLock(memory));
			MultiByteToWideChar(CP_ACP, 0, a_text.c_str(), -1, wide, wideLen);
			GlobalUnlock(memory);

			if (!OpenClipboard(nullptr)) {
				GlobalFree(memory);
				a_error = "the clipboard is in use by another program";
				return false;
			}
			EmptyClipboard();
			if (!SetClipboardData(CF_UNICODETEXT, memory)) {
				CloseClipboard();
				GlobalFree(memory);
				a_error = "Windows refused the clipboard data";
				return false;
			}
			CloseClipboard();  // the clipboard owns the memory now
			return true;
		}
	}

	std::string_view Split(std::string_view a_args, Target& a_target)
	{
		// First '>' outside quotes starts the redirect.
		char        quote = 0;
		std::size_t pos = std::string_view::npos;
		for (std::size_t i = 0; i < a_args.size(); ++i) {
			const char c = a_args[i];
			if (quote) {
				if (quote == '\'' && c == '\\' && i + 1 < a_args.size() && a_args[i + 1] == '\'') {
					++i;
				} else if (c == quote) {
					quote = 0;
				}
			} else if (c == '"' || c == '\'') {
				quote = c;
			} else if (c == '>') {
				pos = i;
				break;
			}
		}
		if (pos == std::string_view::npos) {
			a_target.kind = Kind::None;
			return a_args;
		}

		std::size_t count = 0;
		while (pos + count < a_args.size() && a_args[pos + count] == '>') {
			++count;
		}
		const bool force = count == 1 && pos + 1 < a_args.size() && a_args[pos + 1] == '!';
		const auto rest = Trim(a_args.substr(pos + count + (force ? 1 : 0)));
		const auto command = a_args.substr(0, pos);

		if (count >= 3) {
			a_target.kind = Kind::Clipboard;
			return command;
		}

		a_target.kind = count == 2 ? Kind::Append : Kind::Replace;
		a_target.force = force;
		const auto op = count == 2 ? ">>"sv : force ? ">!"sv : ">"sv;
		if (rest.empty() && a_target.kind == Kind::Append) {
			if (Last().empty()) {
				a_target.error = "'>>' with no file name appends to the last file, but none has been used yet (e.g. >> results)";
			} else {
				a_target.path = Last();
			}
			return command;
		}
		if (auto err = Resolve(rest, a_target.path); !err.empty()) {
			a_target.error = std::format("'{}' {}", op, err);
		} else if (a_target.kind == Kind::Replace && !force && Settings::Get().overwriteNeedsConfirm) {
			std::error_code ec;
			if (std::filesystem::exists(a_target.path, ec)) {
				a_target.error = std::format("'>' {} already exists; use '>!' to replace it, or '>>' to add to it", a_target.path.filename().string());
			}
		}
		return command;
	}

	std::optional<std::filesystem::path> OutputFolder()
	{
		// The game's own folder is the parent of SKSE's log folder.
		const auto logs = SKSE::log::log_directory();
		if (!logs) {
			return std::nullopt;
		}
		return logs->parent_path() / "wp";
	}

	const std::filesystem::path& LastFile()
	{
		return Last();
	}

	bool AllowedExtension(const std::filesystem::path& a_path)
	{
		auto ext = a_path.extension().string();
		std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		constexpr std::array kText{ ".txt"sv, ".csv"sv, ".tsv"sv, ".log"sv, ".md"sv, ".ini"sv, ".toml"sv, ".xml"sv, ".yaml"sv, ".yml"sv };
		return ext.starts_with(".json") || std::ranges::find(kText, ext) != kText.end();
	}

	void Deliver(const Target& a_target, const std::vector<std::string>& a_lines)
	{
		using ConsoleCommands::Print;
		if (a_target.kind == Kind::None || !a_target.error.empty()) {
			return;
		}

		std::string text;
		for (const auto& line : a_lines) {
			text += line;
			text += "\r\n";
		}

		if (a_target.kind == Kind::Clipboard) {
			std::string error;
			if (ToClipboard(text, error)) {
				Print(std::format("  -> copied {} lines to the clipboard", a_lines.size()));
			} else {
				Print(std::format("  -> could not copy to the clipboard: {}", error));
			}
			return;
		}

		std::error_code ec;
		std::filesystem::create_directories(a_target.path.parent_path(), ec);
		std::ofstream out(a_target.path, std::ios::binary | (a_target.kind == Kind::Append ? std::ios::app : std::ios::trunc));
		if (!out) {
			Print(std::format("  -> could not write {}", a_target.path.string()));
			return;
		}
		out << text;
		out.close();
		Last() = a_target.path;
		Print(std::format("  -> {} {} lines to {}", a_target.kind == Kind::Append ? "appended" : "wrote", a_lines.size(), a_target.path.string()));
	}
}
