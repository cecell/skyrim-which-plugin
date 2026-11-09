#include "Redirect.h"

#include "ConsoleCommands.h"

#include <fstream>

namespace Redirect
{
	namespace
	{
		std::filesystem::path& LastFile()
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

		// Documents\My Games\Skyrim Special Edition (the parent of SKSE's log folder).
		std::optional<std::filesystem::path> BaseFolder()
		{
			auto logs = SKSE::log::log_directory();
			if (!logs) {
				return std::nullopt;
			}
			return logs->parent_path();
		}

		// Validates a user-typed file name and makes it absolute. Returns an error message or "".
		std::string Resolve(std::string_view a_name, std::filesystem::path& a_out)
		{
			std::string name{ Trim(a_name) };
			if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front()) {
				name = name.substr(1, name.size() - 2);
			}
			if (name.empty()) {
				return "needs a file name";
			}
			for (char c : name) {
				if (std::string_view{ "<>:\"|?*" }.find(c) != std::string_view::npos) {
					return std::format("'{}' can't be used in a file name", c);
				}
			}
			std::filesystem::path rel{ name };
			if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) {
				return "use a name relative to My Games\\Skyrim Special Edition, not a full path";
			}
			for (const auto& part : rel) {
				if (part == "..") {
					return "'..' isn't allowed; files stay inside My Games\\Skyrim Special Edition";
				}
			}
			if (!rel.has_extension()) {
				rel += ".txt";
			}
			const auto base = BaseFolder();
			if (!base) {
				return "couldn't find the My Games\\Skyrim Special Edition folder";
			}
			a_out = *base / rel;
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
		const auto rest = Trim(a_args.substr(pos + count));
		const auto command = a_args.substr(0, pos);

		if (count >= 3) {
			a_target.kind = Kind::Clipboard;
			return command;
		}

		a_target.kind = count == 2 ? Kind::Append : Kind::Replace;
		if (rest.empty() && a_target.kind == Kind::Append) {
			if (LastFile().empty()) {
				a_target.error = "'>>' with no file name appends to the last file, but none has been used yet (e.g. >> results)";
			} else {
				a_target.path = LastFile();
			}
			return command;
		}
		if (auto err = Resolve(rest, a_target.path); !err.empty()) {
			a_target.error = std::format("'{}' {}", count == 2 ? ">>" : ">", err);
		}
		return command;
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
		LastFile() = a_target.path;
		Print(std::format("  -> {} {} lines to {}", a_target.kind == Kind::Append ? "appended" : "wrote", a_lines.size(), a_target.path.string()));
	}
}
