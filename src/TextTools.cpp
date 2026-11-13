#include "TextTools.h"

#include <charconv>
#include <regex>

namespace TextTools
{
	namespace
	{
		using Lines = std::vector<std::string>;

		std::string Lower(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::optional<std::size_t> Number(std::string_view a_text)
		{
			std::size_t value = 0;
			const auto [end, ec] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
			if (ec != std::errc{} || end != a_text.data() + a_text.size()) {
				return std::nullopt;
			}
			return value;
		}

		// Short flags like -iv. Returns false (with a_error) on a flag not in a_allowed.
		bool Flags(std::string_view a_word, std::string_view a_allowed, std::string& a_flags, std::string_view a_tool, std::string& a_error)
		{
			for (char c : a_word.substr(1)) {
				if (a_allowed.find(c) == std::string_view::npos) {
					a_error = std::format("{}: unknown option -{} (options: {})", a_tool, c, a_allowed.empty() ? "none" : std::format("-{}", a_allowed));
					return false;
				}
				a_flags += c;
			}
			return true;
		}

		bool Has(const std::string& a_flags, char a_flag) { return a_flags.find(a_flag) != std::string::npos; }

		std::regex MakeRegex(const std::string& a_pattern, bool a_icase)
		{
			auto flags = std::regex::ECMAScript;
			if (a_icase) {
				flags |= std::regex::icase;
			}
			return std::regex{ a_pattern, flags };
		}

		// ---------- grep ----------

		std::optional<Lines> Grep(const std::vector<std::string>& a_args, const Lines& a_lines, std::string& a_error)
		{
			std::string                flags;
			std::optional<std::string> pattern;
			for (const auto& word : a_args) {
				if (!pattern && word.size() > 1 && word[0] == '-') {
					if (!Flags(word, "ivcnF", flags, "grep", a_error)) {
						return std::nullopt;
					}
				} else if (!pattern) {
					pattern = word;
				} else {
					a_error = "grep: one pattern only (quote it if it has spaces)";
					return std::nullopt;
				}
			}
			if (!pattern) {
				a_error = "grep [-i -v -c -n -F] PATTERN";
				return std::nullopt;
			}
			const bool icase = Has(flags, 'i');
			const bool fixed = Has(flags, 'F');
			std::regex re;
			if (!fixed) {
				try {
					re = MakeRegex(*pattern, icase);
				} catch (const std::regex_error& e) {
					a_error = std::format("grep: invalid regular expression {} ({})", *pattern, e.what());
					return std::nullopt;
				}
			}
			const auto needle = icase ? Lower(*pattern) : *pattern;
			Lines      out;
			std::size_t count = 0;
			for (std::size_t i = 0; i < a_lines.size(); ++i) {
				const auto& line = a_lines[i];
				const bool  found = fixed ? (icase ? Lower(line) : line).find(needle) != std::string::npos : std::regex_search(line, re);
				if (found == Has(flags, 'v')) {
					continue;
				}
				++count;
				out.push_back(Has(flags, 'n') ? std::format("{}:{}", i + 1, line) : line);
			}
			if (Has(flags, 'c')) {
				return Lines{ std::to_string(count) };
			}
			return out;
		}

		// ---------- sed ----------

		struct Address
		{
			enum class Kind
			{
				None,
				Line,
				Last,
				Regex,
			} kind{ Kind::None };
			std::size_t line{ 0 };
			std::regex  regex;
		};

		struct SedCommand
		{
			Address     from;
			Address     to;  // a range when set
			char        op{ 0 };  // s, d or p
			std::regex  pattern;
			std::string replacement;  // in std::regex_replace's format
			bool        global{ false };
			bool        print{ false };
			bool        inRange{ false };  // state while running
		};

		// sed's replacement (& whole match, \1-\9 groups, \& \\ literal) in std::regex_replace's format.
		std::string Replacement(std::string_view a_text)
		{
			std::string out;
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const char c = a_text[i];
				if (c == '\\' && i + 1 < a_text.size()) {
					const char next = a_text[++i];
					if (next >= '0' && next <= '9') {
						out += '$';
						out += next;
					} else if (next == 'n') {
						out += '\n';
					} else {
						out += next == '$' ? "$$" : std::string(1, next);
					}
				} else if (c == '&') {
					out += "$&";
				} else if (c == '$') {
					out += "$$";
				} else {
					out += c;
				}
			}
			return out;
		}

		// Reads text up to the unescaped a_delim from a_pos (moved past it). \delim becomes delim.
		bool Delimited(std::string_view a_script, std::size_t& a_pos, char a_delim, std::string& a_out)
		{
			for (; a_pos < a_script.size(); ++a_pos) {
				const char c = a_script[a_pos];
				if (c == '\\' && a_pos + 1 < a_script.size() && a_script[a_pos + 1] == a_delim) {
					a_out += a_delim;
					++a_pos;
				} else if (c == a_delim) {
					++a_pos;
					return true;
				} else {
					a_out += c;
				}
			}
			return false;
		}

		bool ParseAddress(std::string_view a_script, std::size_t& a_pos, Address& a_out, std::string& a_error)
		{
			if (a_pos >= a_script.size()) {
				return true;
			}
			const char c = a_script[a_pos];
			if (std::isdigit(static_cast<unsigned char>(c))) {
				std::size_t end = a_pos;
				while (end < a_script.size() && std::isdigit(static_cast<unsigned char>(a_script[end]))) {
					++end;
				}
				a_out.kind = Address::Kind::Line;
				a_out.line = Number(a_script.substr(a_pos, end - a_pos)).value_or(0);
				a_pos = end;
			} else if (c == '$') {
				a_out.kind = Address::Kind::Last;
				++a_pos;
			} else if (c == '/') {
				std::string pattern;
				++a_pos;
				if (!Delimited(a_script, a_pos, '/', pattern)) {
					a_error = "sed: an address /regex/ has no closing /";
					return false;
				}
				try {
					a_out.regex = MakeRegex(pattern, false);
				} catch (const std::regex_error& e) {
					a_error = std::format("sed: invalid regular expression {} ({})", pattern, e.what());
					return false;
				}
				a_out.kind = Address::Kind::Regex;
			}
			return true;
		}

		bool Matches(const Address& a_addr, std::size_t a_index, std::size_t a_count, const std::string& a_line)
		{
			switch (a_addr.kind) {
			case Address::Kind::Line:
				return a_index + 1 == a_addr.line;
			case Address::Kind::Last:
				return a_index + 1 == a_count;
			case Address::Kind::Regex:
				return std::regex_search(a_line, a_addr.regex);
			default:
				return true;
			}
		}

		bool ParseSed(std::string_view a_script, std::vector<SedCommand>& a_out, std::string& a_error)
		{
			std::size_t pos = 0;
			const auto  skipSpace = [&] {
                while (pos < a_script.size() && (a_script[pos] == ' ' || a_script[pos] == '\t')) {
                    ++pos;
                }
			};
			while (true) {
				skipSpace();
				if (pos >= a_script.size()) {
					break;
				}
				SedCommand cmd;
				if (!ParseAddress(a_script, pos, cmd.from, a_error)) {
					return false;
				}
				if (pos < a_script.size() && a_script[pos] == ',' && cmd.from.kind != Address::Kind::None) {
					++pos;
					if (!ParseAddress(a_script, pos, cmd.to, a_error)) {
						return false;
					}
					if (cmd.to.kind == Address::Kind::None) {
						a_error = "sed: a range A,B needs an end address";
						return false;
					}
				}
				skipSpace();
				if (pos >= a_script.size()) {
					a_error = "sed: an address needs a command after it (s, d or p)";
					return false;
				}
				cmd.op = a_script[pos++];
				if (cmd.op == 's') {
					if (pos >= a_script.size()) {
						a_error = "sed: s needs /regex/replacement/";
						return false;
					}
					const char  delim = a_script[pos++];
					std::string pattern, replacement;
					if (!Delimited(a_script, pos, delim, pattern) || !Delimited(a_script, pos, delim, replacement)) {
						a_error = std::format("sed: s{}regex{}replacement{} is missing a {}", delim, delim, delim, delim);
						return false;
					}
					bool icase = false;
					while (pos < a_script.size() && std::string_view{ "gip" }.find(a_script[pos]) != std::string_view::npos) {
						const char f = a_script[pos++];
						cmd.global |= f == 'g';
						icase |= f == 'i';
						cmd.print |= f == 'p';
					}
					try {
						cmd.pattern = MakeRegex(pattern, icase);
					} catch (const std::regex_error& e) {
						a_error = std::format("sed: invalid regular expression {} ({})", pattern, e.what());
						return false;
					}
					cmd.replacement = Replacement(replacement);
				} else if (cmd.op != 'd' && cmd.op != 'p') {
					a_error = std::format("sed: unknown command '{}' (supported: s, d, p)", cmd.op);
					return false;
				}
				skipSpace();
				if (pos < a_script.size()) {
					if (a_script[pos] != ';') {
						a_error = std::format("sed: unexpected '{}' after a command (separate commands with ;)", a_script[pos]);
						return false;
					}
					++pos;
				}
				a_out.push_back(std::move(cmd));
			}
			if (a_out.empty()) {
				a_error = "sed [-n] 'SCRIPT', e.g. sed 's/Iron/Steel/g'";
				return false;
			}
			return true;
		}

		std::optional<Lines> Sed(const std::vector<std::string>& a_args, const Lines& a_lines, std::string& a_error)
		{
			bool                       quiet = false;
			std::optional<std::string> script;
			for (const auto& word : a_args) {
				if (!script && word == "-n") {
					quiet = true;
				} else if (!script) {
					script = word;
				} else {
					a_error = "sed: one script only (quote it, and separate commands with ;)";
					return std::nullopt;
				}
			}
			std::vector<SedCommand> commands;
			if (!script || !ParseSed(*script, commands, a_error)) {
				if (a_error.empty()) {
					a_error = "sed [-n] 'SCRIPT', e.g. sed 's/Iron/Steel/g'";
				}
				return std::nullopt;
			}

			Lines out;
			for (std::size_t i = 0; i < a_lines.size(); ++i) {
				std::string line = a_lines[i];
				bool        deleted = false;
				for (auto& cmd : commands) {
					bool selected = false;
					if (cmd.to.kind == Address::Kind::None) {
						selected = Matches(cmd.from, i, a_lines.size(), line);
					} else if (cmd.inRange) {
						selected = true;
						if (Matches(cmd.to, i, a_lines.size(), line) || (cmd.to.kind == Address::Kind::Line && i + 1 >= cmd.to.line)) {
							cmd.inRange = false;
						}
					} else if (Matches(cmd.from, i, a_lines.size(), line)) {
						selected = true;
						// A line-number end at or before the start ends the range on the start line.
						const bool endsHere = cmd.to.kind == Address::Kind::Line ? i + 1 >= cmd.to.line :
						                                                           cmd.to.kind == Address::Kind::Last && i + 1 == a_lines.size();
						cmd.inRange = !endsHere;
					}
					if (!selected) {
						continue;
					}
					if (cmd.op == 'd') {
						deleted = true;
						break;
					}
					if (cmd.op == 'p') {
						out.push_back(line);
					} else {
						const auto flags = cmd.global ? std::regex_constants::format_default : std::regex_constants::format_first_only;
						if (std::regex_search(line, cmd.pattern)) {
							line = std::regex_replace(line, cmd.pattern, cmd.replacement, flags);
							if (cmd.print) {
								out.push_back(line);
							}
						}
					}
				}
				if (!deleted && !quiet) {
					out.push_back(std::move(line));
				}
			}
			return out;
		}

		// ---------- the rest ----------

		std::optional<Lines> Sort(const std::vector<std::string>& a_args, Lines a_lines, std::string& a_error)
		{
			std::string flags;
			for (const auto& word : a_args) {
				if (word.size() < 2 || word[0] != '-' || !Flags(word, "rnuf", flags, "sort", a_error)) {
					if (a_error.empty()) {
						a_error = "sort [-r -n -u -f]";
					}
					return std::nullopt;
				}
			}
			const bool fold = Has(flags, 'f');
			const auto key = [&](const std::string& a_line) { return fold ? Lower(a_line) : a_line; };
			// -n: by the number the line starts with (lines without one sort first, as in GNU sort).
			const auto number = [](const std::string& a_line) {
				double value = 0;
				const auto start = a_line.find_first_not_of(" \t");
				if (start != std::string::npos) {
					std::from_chars(a_line.data() + start, a_line.data() + a_line.size(), value);
				}
				return value;
			};
			if (Has(flags, 'n')) {
				std::ranges::stable_sort(a_lines, [&](const auto& a, const auto& b) { return number(a) < number(b); });
			} else {
				std::ranges::stable_sort(a_lines, [&](const auto& a, const auto& b) { return key(a) < key(b); });
			}
			if (Has(flags, 'r')) {
				std::ranges::reverse(a_lines);
			}
			if (Has(flags, 'u')) {
				const auto same = [&](const auto& a, const auto& b) { return Has(flags, 'n') ? number(a) == number(b) : key(a) == key(b); };
				a_lines.erase(std::unique(a_lines.begin(), a_lines.end(), same), a_lines.end());
			}
			return a_lines;
		}

		std::optional<Lines> Uniq(const std::vector<std::string>& a_args, const Lines& a_lines, std::string& a_error)
		{
			std::string flags;
			for (const auto& word : a_args) {
				if (word.size() < 2 || word[0] != '-' || !Flags(word, "cdi", flags, "uniq", a_error)) {
					if (a_error.empty()) {
						a_error = "uniq [-c -d -i]";
					}
					return std::nullopt;
				}
			}
			const bool  icase = Has(flags, 'i');
			Lines       out;
			std::size_t i = 0;
			while (i < a_lines.size()) {
				std::size_t j = i + 1;
				while (j < a_lines.size() && (icase ? Lower(a_lines[j]) == Lower(a_lines[i]) : a_lines[j] == a_lines[i])) {
					++j;
				}
				const auto count = j - i;
				if (!Has(flags, 'd') || count > 1) {
					out.push_back(Has(flags, 'c') ? std::format("{:>7} {}", count, a_lines[i]) : a_lines[i]);
				}
				i = j;
			}
			return out;
		}

		std::optional<Lines> HeadTail(bool a_head, const std::vector<std::string>& a_args, const Lines& a_lines, std::string& a_error)
		{
			const auto  name = a_head ? "head" : "tail";
			std::size_t count = 10;
			for (std::size_t i = 0; i < a_args.size(); ++i) {
				const auto& word = a_args[i];
				std::optional<std::size_t> n;
				if (word == "-n" && i + 1 < a_args.size()) {
					n = Number(a_args[++i]);
				} else if (word.size() > 1 && word[0] == '-') {
					n = Number(std::string_view{ word }.substr(word.starts_with("-n") ? 2 : 1));
				}
				if (!n) {
					a_error = std::format("{} [-n N | -N]", name);
					return std::nullopt;
				}
				count = *n;
			}
			count = std::min(count, a_lines.size());
			return a_head ? Lines(a_lines.begin(), a_lines.begin() + static_cast<std::ptrdiff_t>(count)) :
			                Lines(a_lines.end() - static_cast<std::ptrdiff_t>(count), a_lines.end());
		}

		std::optional<Lines> Wc(const std::vector<std::string>& a_args, const Lines& a_lines, std::string& a_error)
		{
			std::string flags;
			for (const auto& word : a_args) {
				if (word.size() < 2 || word[0] != '-' || !Flags(word, "lwc", flags, "wc", a_error)) {
					if (a_error.empty()) {
						a_error = "wc [-l -w -c]";
					}
					return std::nullopt;
				}
			}
			if (flags.empty()) {
				flags = "lwc";
			}
			std::size_t words = 0, chars = 0;
			for (const auto& line : a_lines) {
				chars += line.size() + 1;  // with the line break, as wc counts it
				bool inWord = false;
				for (unsigned char c : line) {
					const bool space = std::isspace(c) != 0;
					words += !space && !inWord;
					inWord = !space;
				}
			}
			std::vector<std::string> parts;
			if (Has(flags, 'l')) {
				parts.push_back(std::to_string(a_lines.size()));
			}
			if (Has(flags, 'w')) {
				parts.push_back(std::to_string(words));
			}
			if (Has(flags, 'c')) {
				parts.push_back(std::to_string(chars));
			}
			std::string line;
			for (const auto& p : parts) {
				line += line.empty() ? p : " " + p;
			}
			return Lines{ line };
		}

		std::optional<Lines> Cat(const std::vector<std::string>& a_args, const Lines& a_lines, std::string& a_error)
		{
			if (a_args.size() > 1 || (a_args.size() == 1 && a_args[0] != "-n")) {
				a_error = "cat [-n]";
				return std::nullopt;
			}
			if (a_args.empty()) {
				return a_lines;
			}
			Lines out;
			for (std::size_t i = 0; i < a_lines.size(); ++i) {
				out.push_back(std::format("{:>6}  {}", i + 1, a_lines[i]));
			}
			return out;
		}
	}

	bool Words(std::string_view a_text, std::vector<std::string>& a_out, std::string& a_error)
	{
		std::string word;
		bool        inWord = false;
		char        quote = 0;
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			const char c = a_text[i];
			if (quote) {
				if (quote == '\'' && c == '\\' && i + 1 < a_text.size() && a_text[i + 1] == '\'') {
					word += '\'';
					++i;
				} else if (c == quote) {
					quote = 0;
				} else {
					word += c;
				}
			} else if (c == '"' || c == '\'') {
				quote = c;
				inWord = true;
			} else if (c == ' ' || c == '\t') {
				if (inWord) {
					a_out.push_back(std::move(word));
					word.clear();
					inWord = false;
				}
			} else {
				word += c;
				inWord = true;
			}
		}
		if (quote) {
			a_error = std::format("missing closing {} quote", quote == '"' ? "double" : "single");
			return false;
		}
		if (inWord) {
			a_out.push_back(std::move(word));
		}
		return true;
	}

	std::optional<std::vector<std::string>> Run(std::string_view a_stage, const std::vector<std::string>& a_lines, std::string& a_error)
	{
		std::vector<std::string> words;
		if (!Words(a_stage, words, a_error)) {
			return std::nullopt;
		}
		if (words.empty()) {
			a_error = "a pipe needs a text tool after it, e.g. | grep sword";
			return std::nullopt;
		}
		const auto tool = Lower(words[0]);
		const std::vector<std::string> args(words.begin() + 1, words.end());
		if (tool == "grep") {
			return Grep(args, a_lines, a_error);
		}
		if (tool == "sed") {
			return Sed(args, a_lines, a_error);
		}
		if (tool == "sort") {
			return Sort(args, a_lines, a_error);
		}
		if (tool == "uniq") {
			return Uniq(args, a_lines, a_error);
		}
		if (tool == "head" || tool == "tail") {
			return HeadTail(tool == "head", args, a_lines, a_error);
		}
		if (tool == "wc") {
			return Wc(args, a_lines, a_error);
		}
		if (tool == "cat") {
			return Cat(args, a_lines, a_error);
		}
		if (tool == "echo") {
			std::string line;
			for (const auto& w : args) {
				line += line.empty() ? w : " " + w;
			}
			return std::vector<std::string>{ line };
		}
		a_error = std::format("unknown text tool '{}': use grep, sed, sort, uniq, head, tail, wc, echo or cat", words[0]);
		return std::nullopt;
	}
}
