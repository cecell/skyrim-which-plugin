#include "Query.h"

#include <regex>

namespace Query
{
	namespace
	{
		struct FieldWord
		{
			std::string_view word;
			Field            field;
		};

		constexpr std::array kFields{
			FieldWord{ "plugin-name"sv, Field::PluginName },
			FieldWord{ "plugin-master"sv, Field::PluginMaster },
			FieldWord{ "lobytes"sv, Field::LoBytes },
			FieldWord{ "formid"sv, Field::FormId },
			FieldWord{ "edid"sv, Field::EditorId },
			FieldWord{ "type"sv, Field::Type },
			FieldWord{ "keyword"sv, Field::Keyword },
			FieldWord{ "name"sv, Field::Name },
			FieldWord{ "refs"sv, Field::Refs },
			FieldWord{ "base"sv, Field::Base },
			FieldWord{ "contents"sv, Field::Contents },
		};

		// Characters Windows forbids in file names (so they can never be in a plugin name).
		constexpr std::string_view kBadFileNameChars = "<>:\"/\\|?*";

		std::string_view Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t");
			return a_text.substr(first, last - first + 1);
		}

		std::string Lower(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::string Quoted(std::string_view a_chars)
		{
			std::string out;
			for (char c : a_chars) {
				if (!out.empty()) {
					out += ' ';
				}
				out += '\'';
				out += c;
				out += '\'';
			}
			return out;
		}

		char OpChar(Match a_match)
		{
			return a_match == Match::Exact ? '=' : a_match == Match::Regex ? '~' : ':';
		}

		// Splits on '||' (OR groups) and '&&' (AND terms) outside quotes.
		// Returns false (with an error) on an unterminated quote.
		bool SplitTerms(std::string_view a_text, std::vector<std::vector<std::string_view>>& a_out, std::vector<std::string>& a_errors)
		{
			a_out.emplace_back();
			std::size_t start = 0;
			char        quote = 0;
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const char c = a_text[i];
				if (quote) {
					if (quote == '\'' && c == '\\' && i + 1 < a_text.size() && a_text[i + 1] == '\'') {
						++i;  // escaped apostrophe
					} else if (c == quote) {
						quote = 0;
					}
				} else if (c == '"' || c == '\'') {
					quote = c;
				} else if ((c == '&' || c == '|') && i + 1 < a_text.size() && a_text[i + 1] == c) {
					a_out.back().push_back(a_text.substr(start, i - start));
					if (c == '|') {
						a_out.emplace_back();
					}
					++i;
					start = i + 1;
				}
			}
			if (quote) {
				a_errors.push_back(std::format("Missing closing {} quote", quote == '"' ? "double" : "single"));
				return false;
			}
			a_out.back().push_back(a_text.substr(start));
			return true;
		}

		std::string BadFileNameChars(std::string_view a_value)
		{
			std::string bad;
			for (char c : a_value) {
				if (kBadFileNameChars.find(c) != std::string_view::npos && bad.find(c) == std::string::npos) {
					bad += c;
				}
			}
			return bad;
		}

		void Validate(Term& a_term, Parsed& a_out)
		{
			const auto& v = a_term.value;
			const auto  label = std::format("{}{}{}", FieldName(a_term.field), OpChar(a_term.match), v);

			if (a_term.match == Match::Regex) {
				try {
					std::regex test{ v, std::regex::ECMAScript | std::regex::icase };
				} catch (const std::regex_error& e) {
					a_out.errors.push_back(std::format("{} - invalid regular expression ({})", label, e.what()));
				}
				return;
			}

			switch (a_term.field) {
			case Field::LoBytes:
				if (auto bad = NonHexChars(v); !bad.empty()) {
					a_out.errors.push_back(std::format("{} - load-order bytes are hex (0-9, A-F); not allowed: {}", label, Quoted(bad)));
				} else if (v.size() > 5) {
					a_out.errors.push_back(std::format("{} - too long: full slots are 2 digits (00-FD), light slots FE + 3 digits (FE000-FEFFF)", label));
				} else if (v.size() > 2 && !Lower(v).starts_with("fe")) {
					a_out.errors.push_back(std::format("{} - only light slots (FExxx) have more than 2 digits", label));
				}
				break;
			case Field::FormId:
				{
					const auto digits = StripHexPrefix(v);
					if (auto bad = NonHexChars(digits); !bad.empty()) {
						a_out.errors.push_back(std::format("{} - form IDs are hex (0-9, A-F); not allowed: {}", label, Quoted(bad)));
					} else if (digits.empty() || digits.size() > 8) {
						a_out.errors.push_back(std::format("{} - a form ID is 1 to 8 hex digits", label));
					}
					break;
				}
			case Field::PluginName:
			case Field::PluginMaster:
				if (auto bad = BadFileNameChars(v); !bad.empty()) {
					a_out.errors.push_back(std::format("{} - {} can't appear in a plugin file name, so nothing could match (use ~ for a regex)", label, Quoted(bad)));
				}
				break;
			case Field::Type:
				for (unsigned char c : v) {
					if (!std::isalnum(c) && c != '_') {
						a_out.errors.push_back(std::format("{} - record types are letters, digits and _ (e.g. WEAP, NPC_)", label));
						break;
					}
				}
				break;
			case Field::EditorId:
				{
					std::string bad;
					for (unsigned char c : v) {
						if (!std::isalnum(c) && c != '_' && bad.find(static_cast<char>(c)) == std::string::npos) {
							bad += static_cast<char>(c);
						}
					}
					if (!bad.empty()) {
						a_out.warnings.push_back(std::format("{} - editor IDs normally only contain letters, digits and _; unusual: {}", label, Quoted(bad)));
					}
					break;
				}
			default:
				break;
			}
		}

		// Reads a quoted or bare value. Returns false (with an error) on trailing junk or empty quotes.
		bool ReadValue(std::string_view a_rest, std::string_view a_source, std::string& a_value, Parsed& a_out)
		{
			if (a_rest.empty()) {
				a_out.errors.push_back(std::format("{} - nothing to search for", a_source));
				return false;
			}
			if (a_rest.front() == '"' || a_rest.front() == '\'') {
				const char  quote = a_rest.front();
				std::size_t i = 1;
				for (; i < a_rest.size(); ++i) {
					const char c = a_rest[i];
					if (quote == '\'' && c == '\\' && i + 1 < a_rest.size() && a_rest[i + 1] == '\'') {
						a_value += '\'';
						++i;
					} else if (c == quote) {
						break;
					} else {
						a_value += c;
					}
				}
				if (!Trim(a_rest.substr(std::min(i + 1, a_rest.size()))).empty()) {
					a_out.errors.push_back(std::format("{} - unexpected text after the closing quote", a_source));
					return false;
				}
			} else {
				a_value = std::string{ a_rest };
			}
			if (a_value.empty()) {
				a_out.errors.push_back(std::format("{} - empty quotes", a_source));
				return false;
			}
			return true;
		}

		void ParseTerm(std::string_view a_raw, Parsed& a_out)
		{
			const auto source = Trim(a_raw);
			Term       term;
			term.source = std::string{ source };
			if (source.empty()) {
				a_out.errors.push_back("Empty search term (&& or || at the start or end, or two in a row)");
				return;
			}

			std::string_view rest = source;
			if (rest.starts_with('!')) {
				term.negated = true;
				rest = Trim(rest.substr(1));
			}

			// Field prefix: a word of letters and '-' followed by ':', '=' or '~' (before any quote).
			std::size_t wordEnd = 0;
			while (wordEnd < rest.size() && (std::isalpha(static_cast<unsigned char>(rest[wordEnd])) || rest[wordEnd] == '-')) {
				++wordEnd;
			}
			if (wordEnd > 0 && wordEnd < rest.size() && std::string_view{ ":=~" }.find(rest[wordEnd]) != std::string_view::npos) {
				const auto word = Lower(rest.substr(0, wordEnd));
				const char op = rest[wordEnd];
				const auto it = std::ranges::find_if(kFields, [&](const FieldWord& f) { return f.word == word; });
				if (it != kFields.end()) {
					term.field = it->field;
					term.match = op == '=' ? Match::Exact : op == '~' ? Match::Regex : Match::Contains;
				} else if (word == "regex" && op == ':') {
					term.field = Field::PluginName;
					term.match = Match::Regex;
				} else if (word == "fe" && op == ':') {
					// xEdit-style light slot typed as FE:012 - the load-order bytes FE012.
					term.field = Field::LoBytes;
					term.value = std::format("FE{}", Trim(rest.substr(wordEnd + 1)));
					Validate(term, a_out);
					a_out.terms.push_back(std::move(term));
					return;
				} else {
					a_out.errors.push_back(std::format(
						"{} - unknown field '{}'. Use plugin-name, plugin-master, lobytes, formid, edid, name, type, keyword, refs, base or contents (quote the text if it isn't a field)",
						source, rest.substr(0, wordEnd)));
					return;
				}
				rest = Trim(rest.substr(wordEnd + 1));
			}

			if (!ReadValue(rest, source, term.value, a_out)) {
				return;
			}
			Validate(term, a_out);
			a_out.terms.push_back(std::move(term));
		}
	}

	Parsed Parse(std::string_view a_text)
	{
		Parsed     out;
		const auto text = Trim(a_text);
		if (text.empty()) {
			return out;
		}
		std::vector<std::vector<std::string_view>> raw;
		if (!SplitTerms(text, raw, out.errors)) {
			return out;
		}
		for (const auto& group : raw) {
			const auto before = out.terms.size();
			for (auto part : group) {
				ParseTerm(part, out);
			}
			out.groups.emplace_back(out.terms.begin() + static_cast<std::ptrdiff_t>(before), out.terms.end());
		}
		return out;
	}

	std::string_view FieldName(Field a_field)
	{
		switch (a_field) {
		case Field::PluginName:
			return "plugin-name";
		case Field::PluginMaster:
			return "plugin-master";
		case Field::LoBytes:
			return "lobytes";
		case Field::FormId:
			return "formid";
		case Field::EditorId:
			return "edid";
		case Field::Type:
			return "type";
		case Field::Keyword:
			return "keyword";
		case Field::Name:
			return "name";
		case Field::Refs:
			return "refs";
		case Field::Base:
			return "base";
		case Field::Contents:
			return "contents";
		default:
			return "any";
		}
	}

	bool IsRecordField(Field a_field)
	{
		switch (a_field) {
		case Field::FormId:
		case Field::EditorId:
		case Field::Type:
		case Field::Keyword:
		case Field::Name:
		case Field::Refs:
		case Field::Base:
		case Field::Contents:
			return true;
		default:
			return false;
		}
	}

	bool IsLookupField(Field a_field)
	{
		return a_field == Field::Refs || a_field == Field::Base || a_field == Field::Contents;
	}

	bool IsHex(std::string_view a_text)
	{
		return !a_text.empty() && NonHexChars(a_text).empty();
	}

	std::string NonHexChars(std::string_view a_text)
	{
		std::string bad;
		for (unsigned char c : a_text) {
			if (!std::isxdigit(c) && bad.find(static_cast<char>(c)) == std::string::npos) {
				bad += static_cast<char>(c);
			}
		}
		return bad;
	}

	std::optional<std::uint32_t> ParseHex(std::string_view a_text)
	{
		if (!IsHex(a_text) || a_text.size() > 8) {
			return std::nullopt;
		}
		std::uint32_t value = 0;
		for (unsigned char c : a_text) {
			value = (value << 4) | static_cast<std::uint32_t>(std::isdigit(c) ? c - '0' : std::tolower(c) - 'a' + 10);
		}
		return value;
	}

	std::string StripHexPrefix(std::string_view a_text)
	{
		if (a_text.size() > 2 && (a_text.starts_with("0x") || a_text.starts_with("0X"))) {
			a_text.remove_prefix(2);
		}
		return std::string{ a_text };
	}
}
