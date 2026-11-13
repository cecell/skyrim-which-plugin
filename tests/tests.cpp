// Unit tests for the parts of wp that don't need the game: the search parser, the pipe splitter
// and the text tools. Run by ctest (CI) or directly: build\wp_tests.exe

#include "Pipeline.h"
#include "Query.h"
#include "TextTools.h"

#include <iostream>

namespace
{
	int g_failures = 0;
	int g_checks = 0;

	void Check(bool a_ok, std::string_view a_what)
	{
		++g_checks;
		if (!a_ok) {
			++g_failures;
			std::cerr << "FAIL: " << a_what << '\n';
		}
	}

	using Lines = std::vector<std::string>;

	std::string Show(const Lines& a_lines)
	{
		std::string out = "[";
		for (const auto& l : a_lines) {
			out += (out.size() > 1 ? ", \"" : "\"") + l + "\"";
		}
		return out + "]";
	}

	// The tool's output must equal a_expected.
	void Tool(std::string_view a_stage, const Lines& a_in, const Lines& a_expected)
	{
		std::string error;
		const auto  out = TextTools::Run(a_stage, a_in, error);
		const bool  ok = out && *out == a_expected;
		Check(ok, std::format("{} on {} gave {} (error: {}), expected {}", a_stage, Show(a_in), out ? Show(*out) : "nothing", error, Show(a_expected)));
	}

	// The tool must refuse with an error.
	void ToolError(std::string_view a_stage)
	{
		std::string error;
		const auto  out = TextTools::Run(a_stage, Lines{ "x" }, error);
		Check(!out && !error.empty(), std::format("{} should be an error", a_stage));
	}

	void QueryTests()
	{
		using Query::Field;
		using Query::Match;

		auto p = Query::Parse(R"(edid~"^Iron" && plugin-master:dawnguard || !type=WEAP)");
		Check(p.errors.empty() && p.groups.size() == 2 && p.groups[0].size() == 2 && p.groups[1].size() == 1, "AND binds tighter than OR");
		Check(p.groups[0][0].field == Field::EditorId && p.groups[0][0].match == Match::Regex && p.groups[0][0].value == "^Iron", "edid~ regex term");
		Check(p.groups[1][0].negated && p.groups[1][0].field == Field::Type && p.groups[1][0].match == Match::Exact, "negated exact type");

		p = Query::Parse(R"(name:"iron sword")");
		Check(p.errors.empty() && p.terms.size() == 1 && p.terms[0].field == Field::Name && p.terms[0].value == "iron sword", "name: with quotes");
		Check(Query::IsRecordField(Field::Name), "name searches records");

		for (const auto& [text, field] : { std::pair{ "refs:IronSword"sv, Field::Refs }, std::pair{ "base:00000014"sv, Field::Base }, std::pair{ "contents:LItemWeaponSword"sv, Field::Contents } }) {
			p = Query::Parse(text);
			Check(p.errors.empty() && p.terms.size() == 1 && p.terms[0].field == field && Query::IsLookupField(field) && Query::IsRecordField(field), std::format("{} parses", text));
		}

		p = Query::Parse(R"('it\'s')");
		Check(p.errors.empty() && p.terms.size() == 1 && p.terms[0].value == "it's", "single quotes with an escaped apostrophe");

		Check(!Query::Parse("bogus:x").errors.empty(), "unknown field is an error");
		Check(!Query::Parse("lobytes:XZ").errors.empty(), "non-hex load-order bytes are an error");
		Check(!Query::Parse("edid~\"[\"").errors.empty(), "invalid regex is an error");
		Check(!Query::Parse("a &&").errors.empty(), "dangling && is an error");
		Check(!Query::Parse("\"open").errors.empty(), "missing closing quote is an error");

		p = Query::Parse("fe:012");
		Check(p.errors.empty() && p.terms.size() == 1 && p.terms[0].field == Field::LoBytes && p.terms[0].value == "FE012", "fe:012 is a light slot");

		Check(Query::ParseHex("00012EB7") == 0x12EB7u && !Query::ParseHex("12345678A") && !Query::ParseHex("xyz"), "ParseHex");
		Check(Query::StripHexPrefix("0x1F") == "1F", "StripHexPrefix");
	}

	void PipelineTests()
	{
		auto s = Pipeline::Split(R"(type=WEAP || type=ARMO | grep "a|b" | sort)");
		Check(s.size() == 3 && s[0] == "type=WEAP || type=ARMO " && s[1] == R"( grep "a|b" )" && s[2] == " sort", "pipe split keeps || and quoted |");
		Check(Pipeline::Split("abc").size() == 1, "no pipe, one stage");
	}

	void TextToolTests()
	{
		const Lines swords{ "IronSword", "SteelSword", "ironmace", "DaedricSword" };

		Tool("grep Sword", swords, { "IronSword", "SteelSword", "DaedricSword" });
		Tool("grep -i iron", swords, { "IronSword", "ironmace" });
		Tool("grep -v Sword", swords, { "ironmace" });
		Tool("grep -c Sword", swords, { "3" });
		Tool("grep -in IRON", swords, { "1:IronSword", "3:ironmace" });
		Tool("grep -F .", Lines{ "a.b", "ab" }, { "a.b" });
		Tool("grep '^S'", swords, { "SteelSword" });
		Tool("grep \"Steel|Daedric\"", swords, { "SteelSword", "DaedricSword" });
		ToolError("grep");
		ToolError("grep -z x");
		ToolError("grep [");

		Tool("sed s/Sword/Blade/", swords, { "IronBlade", "SteelBlade", "ironmace", "DaedricBlade" });
		Tool("sed 's/o/0/g'", Lines{ "foo boo" }, { "f00 b00" });
		Tool("sed 's/O/0/'", Lines{ "foo" }, { "foo" });
		Tool("sed 's/O/0/i'", Lines{ "foo" }, { "f0o" });
		Tool(R"(sed 's/(\w+)Sword/\1 [&]/')", Lines{ "IronSword" }, { "Iron [IronSword]" });
		Tool("sed 's|/|-|g'", Lines{ "a/b/c" }, { "a-b-c" });
		Tool("sed 's/x/$/'", Lines{ "axb" }, { "a$b" });
		Tool("sed 2d", swords, { "IronSword", "ironmace", "DaedricSword" });
		Tool("sed '$d'", swords, { "IronSword", "SteelSword", "ironmace" });
		Tool("sed /mace/d", swords, { "IronSword", "SteelSword", "DaedricSword" });
		Tool("sed 2,3d", swords, { "IronSword", "DaedricSword" });
		Tool("sed '/Steel/,/Daedric/d'", swords, { "IronSword" });
		Tool("sed -n /Sword/p", swords, { "IronSword", "SteelSword", "DaedricSword" });
		Tool("sed -n 's/Iron/Steel/p'", swords, { "SteelSword" });
		Tool("sed 's/Sword//; /^$/d; s/^/- /'", Lines{ "IronSword", "Sword" }, { "- Iron" });
		ToolError("sed");
		ToolError("sed s/a/b");
		ToolError("sed x");
		ToolError("sed '2,d'");

		Tool("sort", Lines{ "b", "C", "a" }, { "C", "a", "b" });
		Tool("sort -f", Lines{ "b", "C", "a" }, { "a", "b", "C" });
		Tool("sort -r", Lines{ "a", "c", "b" }, { "c", "b", "a" });
		Tool("sort -n", Lines{ "10 x", "9 y", "100 z" }, { "9 y", "10 x", "100 z" });
		Tool("sort -u", Lines{ "b", "a", "b" }, { "a", "b" });
		ToolError("sort -q");

		Tool("uniq", Lines{ "a", "a", "b", "a" }, { "a", "b", "a" });
		Tool("uniq -c", Lines{ "a", "a", "b" }, { "      2 a", "      1 b" });
		Tool("uniq -d", Lines{ "a", "a", "b" }, { "a" });
		Tool("uniq -i", Lines{ "A", "a" }, { "A" });

		Lines twelve;
		for (int i = 1; i <= 12; ++i) {
			twelve.push_back(std::to_string(i));
		}
		Tool("head", twelve, Lines(twelve.begin(), twelve.begin() + 10));
		Tool("head -3", twelve, { "1", "2", "3" });
		Tool("head -n 2", twelve, { "1", "2" });
		Tool("tail -n2", twelve, { "11", "12" });
		Tool("tail -20", Lines{ "x" }, { "x" });
		ToolError("head -n");

		Tool("wc -l", swords, { "4" });
		Tool("wc", Lines{ "a b", "c" }, { "2 3 6" });
		Tool("echo hello world", swords, { "hello world" });
		Tool("cat", swords, swords);
		Tool("cat -n", Lines{ "x" }, { "     1  x" });
		ToolError("frobnicate");

		std::vector<std::string> words;
		std::string              error;
		Check(TextTools::Words(R"(sed -n 's/a b/c/'  "x y")", words, error) && words == Lines{ "sed", "-n", "s/a b/c/", "x y" }, "Words splits like a shell");
		words.clear();
		Check(!TextTools::Words("grep 'oops", words, error), "Words: missing quote is an error");
	}
}

int main()
{
	QueryTests();
	PipelineTests();
	TextToolTests();
	std::cout << std::format("{} checks, {} failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
