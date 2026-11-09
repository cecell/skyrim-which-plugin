#include "WhichPlugin.h"

#include "ConsoleCommands.h"
#include "ConsoleHook.h"
#include "LoadOrder.h"
#include "Query.h"

#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace WhichPlugin
{
	namespace
	{
		constexpr auto        LONG_NAME = "WhichPlugin";
		constexpr auto        SHORT_NAME = "wp";
		constexpr std::size_t kConsoleCap = 100;  // records shown in the console; redirects get all

		using ConsoleCommands::Print;
		using Query::Field;
		using Query::Match;
		using Query::Term;

		// ---------- text helpers ----------

		std::string Lower(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::string Upper(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return out;
		}

		bool IContains(std::string_view a_haystack, std::string_view a_needle)
		{
			return Lower(a_haystack).find(Lower(a_needle)) != std::string::npos;
		}

		// An unscoped value "looks like" load-order bytes if it is 1-5 hex digits.
		bool LooksLikeSlot(std::string_view a_value)
		{
			return Query::IsHex(a_value) && a_value.size() <= 5;
		}

		// An unscoped value is a form ID if it is 8 hex digits, or any hex with a 0x prefix.
		bool LooksLikeFormId(std::string_view a_value)
		{
			const bool prefixed = a_value.size() > 2 && (a_value.starts_with("0x") || a_value.starts_with("0X"));
			const auto digits = Query::StripHexPrefix(a_value);
			return Query::IsHex(digits) && digits.size() <= 8 && (prefixed || digits.size() == 8);
		}

		std::string FormIdText(RE::FormID a_id) { return std::format("{:08X}", a_id); }

		// The load-order bytes at the start of a form ID: "0A", "FE012", or "FF" for runtime forms.
		std::string FormLoBytes(RE::FormID a_id)
		{
			const auto top = a_id >> 24;
			if (top == 0xFE) {
				return std::format("FE{:03X}", (a_id >> 12) & 0xFFF);
			}
			return std::format("{:02X}", top);
		}

		// ---------- editor IDs ----------

		// GetFormEditorID() covers everything when po3's Tweaks is installed; otherwise fall back to
		// the game's editor-ID map (built into a reverse lookup once per command). The map's form
		// pointers are only used as lookup keys, never dereferenced: some of them are stale.
		class EditorIds
		{
		public:
			std::string Get(const RE::TESForm* a_form)
			{
				if (const auto* edid = a_form->GetFormEditorID(); edid && *edid) {
					return edid;
				}
				if (!_built) {
					Build();
				}
				const auto it = _byForm.find(a_form);
				return it != _byForm.end() ? it->second : std::string{};
			}

		private:
			void Build()
			{
				_built = true;
				const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
				const RE::BSReadLockGuard guard{ lock };
				if (map) {
					for (const auto& [key, form] : *map) {
						if (form) {
							_byForm.emplace(form, key.c_str());
						}
					}
				}
			}

			std::unordered_map<const RE::TESForm*, std::string> _byForm;
			bool                                                _built{ false };
		};

		// ---------- matching ----------

		struct Matcher
		{
			const Term*               term;
			std::string               value;  // normalized: upper hex for formid/lobytes
			std::optional<std::regex> regex;

			explicit Matcher(const Term& a_term) :
				term(&a_term), value(a_term.value)
			{
				if (a_term.match == Match::Regex) {
					regex.emplace(a_term.value, std::regex::ECMAScript | std::regex::icase);
				} else if (a_term.field == Field::FormId) {
					value = FormIdText(Query::ParseHex(Query::StripHexPrefix(a_term.value)).value_or(0));
				} else if (a_term.field == Field::LoBytes) {
					value = Upper(a_term.value);
				}
			}

			bool Test(std::string_view a_text) const
			{
				switch (term->match) {
				case Match::Regex:
					return std::regex_search(a_text.begin(), a_text.end(), *regex);
				case Match::Exact:
					return Lower(a_text) == Lower(value);
				default:  // Contains
					if (term->field == Field::LoBytes) {
						return Upper(a_text).starts_with(value);
					}
					if (term->field == Field::FormId) {
						return Upper(a_text) == value;
					}
					return IContains(a_text, value);
				}
			}

			// Unscoped: name contains, or (if hex) load-order slot starts with.
			bool TestAny(const RE::TESFile* a_file) const
			{
				if (!a_file) {
					return false;
				}
				return IContains(a_file->GetFilename(), term->value) ||
				       (LooksLikeSlot(term->value) && LoadOrder::SlotHex(a_file).starts_with(Upper(term->value)));
			}

			std::vector<const RE::BGSKeyword*> keywords;  // keyword terms: the keywords it refers to
		};

		// The keywords a keyword term refers to. ':' and '=' name one keyword by form ID or editor ID;
		// '~' means every keyword whose editor ID matches. Uses the game's live keyword list (never the
		// editor-ID map, which holds stale pointers). Empty result: a_error says why.
		std::vector<const RE::BGSKeyword*> ResolveKeywords(const Term& a_term, EditorIds& a_edids, std::string& a_error)
		{
			std::vector<const RE::BGSKeyword*> out;
			const auto&                        all = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSKeyword>();

			if (a_term.match == Match::Regex) {
				const std::regex re{ a_term.value, std::regex::ECMAScript | std::regex::icase };
				for (const auto* kw : all) {
					if (kw) {
						const auto edid = a_edids.Get(kw);
						if (!edid.empty() && std::regex_search(edid, re)) {
							out.push_back(kw);
						}
					}
				}
				if (out.empty()) {
					a_error = std::format("keyword~{} - no keyword's editor ID matches", a_term.value);
				}
				return out;
			}

			const auto digits = Query::StripHexPrefix(a_term.value);
			if (Query::IsHex(digits) && digits.size() <= 8) {
				if (auto* form = RE::TESForm::LookupByID(Query::ParseHex(digits).value_or(0)); form && form->Is(RE::FormType::Keyword)) {
					out.push_back(form->As<RE::BGSKeyword>());
					return out;
				}
			}
			const auto wanted = Lower(a_term.value);
			for (const auto* kw : all) {
				if (kw && Lower(a_edids.Get(kw)) == wanted) {
					out.push_back(kw);
					return out;
				}
			}
			a_error = std::format("keyword:{} - no keyword has that form ID or editor ID", a_term.value);
			return out;
		}

		std::vector<std::vector<Matcher>> BuildMatchers(const std::vector<std::vector<Term>>& a_groups, EditorIds& a_edids)
		{
			std::vector<std::vector<Matcher>> out;
			for (const auto& group : a_groups) {
				auto& g = out.emplace_back();
				for (const auto& t : group) {
					auto& m = g.emplace_back(t);
					if (t.field == Field::Keyword) {
						std::string unused;
						m.keywords = ResolveKeywords(t, a_edids, unused);
					}
				}
			}
			return out;
		}

		bool MatchesPlugin(const Matcher& a_m, const RE::TESFile* a_file)
		{
			bool hit = false;
			switch (a_m.term->field) {
			case Field::PluginName:
				hit = a_m.Test(a_file->GetFilename());
				break;
			case Field::PluginMaster:
				for (std::uint32_t i = 0; i < a_file->masterCount && !hit; ++i) {
					if (const auto* master = a_file->masterPtrs ? a_file->masterPtrs[i] : nullptr) {
						hit = a_m.Test(master->GetFilename());
					}
				}
				break;
			case Field::LoBytes:
				hit = a_m.Test(LoadOrder::SlotHex(a_file));
				break;
			default:
				hit = a_m.TestAny(a_file);
				break;
			}
			return hit != a_m.term->negated;
		}

		bool MatchesRecord(const Matcher& a_m, const RE::TESForm* a_form, EditorIds& a_edids)
		{
			const auto* last = a_form->GetFile(-1);
			const auto* master = a_form->GetFile(0);
			bool        hit = false;
			switch (a_m.term->field) {
			case Field::PluginName:
				hit = last && a_m.Test(last->GetFilename());
				break;
			case Field::PluginMaster:
				hit = master && a_m.Test(master->GetFilename());
				break;
			case Field::LoBytes:
				hit = a_m.Test(FormLoBytes(a_form->GetFormID()));
				break;
			case Field::FormId:
				hit = a_m.Test(FormIdText(a_form->GetFormID()));
				break;
			case Field::EditorId:
				hit = a_m.Test(a_edids.Get(a_form));
				break;
			case Field::Type:
				hit = a_m.Test(RE::FormTypeToString(a_form->GetFormType()));
				break;
			case Field::Keyword:
				if (const auto* kf = a_form->As<RE::BGSKeywordForm>()) {
					hit = std::ranges::any_of(a_m.keywords, [&](const RE::BGSKeyword* kw) { return kf->HasKeyword(kw); });
				}
				break;
			default:
				hit = a_m.TestAny(last);
				break;
			}
			return hit != a_m.term->negated;
		}

		// ---------- descriptions ----------

		std::string Describe(const Term& a_term, bool a_records)
		{
			const auto& v = a_term.value;
			std::string subject;
			switch (a_term.field) {
			case Field::PluginName:
				subject = a_records ? "last plugin to edit it" : "plugin name";
				break;
			case Field::PluginMaster:
				subject = a_records ? "its master plugin" : "one of its masters";
				break;
			case Field::LoBytes:
				subject = a_records ? "form ID load-order bytes" : "load-order slot";
				break;
			case Field::FormId:
				subject = "form ID";
				break;
			case Field::EditorId:
				subject = "editor ID";
				break;
			case Field::Type:
				subject = "record type";
				break;
			case Field::Keyword:
				{
					const auto what = a_term.match == Match::Regex ?
					                      std::format("has a keyword whose editor ID matches /{}/", v) :
					                      std::format("has keyword {}", v);
					return a_term.negated ? std::format("NOT ({})", what) : what;
				}
			default:
				{
					const auto who = a_records ? "last plugin to edit it" : "plugin";
					const auto what = LooksLikeSlot(v) ?
					                      std::format("{}: name contains \"{}\" or slot starts with {}", who, v, Upper(v)) :
					                      std::format("{}: name contains \"{}\"", who, v);
					return a_term.negated ? std::format("NOT ({})", what) : what;
				}
			}

			std::string how;
			if (a_term.match == Match::Regex) {
				how = std::format("matches /{}/", v);
			} else if (a_term.match == Match::Exact) {
				how = std::format("is \"{}\"", v);
			} else if (a_term.field == Field::LoBytes) {
				how = std::format("starts with {}", Upper(v));
			} else if (a_term.field == Field::FormId) {
				how = std::format("is {}", FormIdText(Query::ParseHex(Query::StripHexPrefix(v)).value_or(0)));
			} else {
				how = std::format("contains \"{}\"", v);
			}
			const auto what = std::format("{} {}", subject, how);
			return a_term.negated ? std::format("NOT ({})", what) : what;
		}

		void PrintCriteria(const std::vector<std::vector<Term>>& a_groups, bool a_records)
		{
			Print(a_records ? "  Records where:" : "  Plugins where:");
			for (std::size_t g = 0; g < a_groups.size(); ++g) {
				const auto& group = a_groups[g];
				const bool  bracket = a_groups.size() > 1 && group.size() > 1;
				for (std::size_t i = 0; i < group.size(); ++i) {
					std::string prefix = i == 0 ? (g == 0 ? "" : "OR ") : "    AND ";
					if (i == 0 && bracket) {
						prefix += "( ";
					}
					const bool last = i + 1 == group.size();
					Print(std::format("    {}{}{}", prefix, Describe(group[i], a_records), last && bracket ? " )" : ""));
				}
			}
		}

		// ---------- options ----------

		enum class Layout
		{
			Default,
			Concise,  // -c: just the IDs (records) or file names (plugins), nothing else
			Detail,   // -d: one aligned row per result, like `ls -l`
		};

		struct Options
		{
			Layout layout{ Layout::Default };
			bool   loAgnostic{ false };  // -l: 0x00E8F0, "Dawnguard.esm" (GetFormFromFile style)
			bool   asInt{ false };       // --int: decimal instead of hex
		};

		// Leading options, Linux style: -c/--concise, -d/--detail, -l/--lo-agnostic, --int. Short options
		// combine (-cl). A bare "--" ends the options. Returns the rest of the line.
		std::string_view ParseOptions(std::string_view a_args, Options& a_opts, std::string& a_error)
		{
			bool layoutSet = false;
			auto setLayout = [&](Layout a_layout) {
				if (layoutSet && a_opts.layout != a_layout) {
					a_error = "-c (concise) and -d (detail) can't be combined";
					return false;
				}
				a_opts.layout = a_layout;
				layoutSet = true;
				return true;
			};
			auto unknown = [&](std::string_view a_token) {
				a_error = std::format("unknown option '{}'. Options: -c/--concise, -d/--detail, -l/--lo-agnostic, --int. "
									  "(NOT is '!', e.g. !patch; put -- first to search for text that starts with '-')",
					a_token);
			};

			std::string_view rest = a_args;
			while (true) {
				const auto start = rest.find_first_not_of(" \t");
				if (start == std::string_view::npos) {
					return {};
				}
				rest.remove_prefix(start);
				if (!rest.starts_with('-')) {
					return rest;
				}
				const auto end = rest.find_first_of(" \t");
				const auto token = rest.substr(0, end);
				const auto after = end == std::string_view::npos ? std::string_view{} : rest.substr(end);
				if (token == "--") {
					return after;
				}
				if (token.starts_with("--")) {
					const auto name = Lower(token.substr(2));
					if (name == "concise") {
						if (!setLayout(Layout::Concise)) {
							return {};
						}
					} else if (name == "detail") {
						if (!setLayout(Layout::Detail)) {
							return {};
						}
					} else if (name == "lo-agnostic") {
						a_opts.loAgnostic = true;
					} else if (name == "int") {
						a_opts.asInt = true;
					} else {
						unknown(token);
						return {};
					}
				} else {
					for (char c : token.substr(1)) {
						switch (std::tolower(static_cast<unsigned char>(c))) {
						case 'c':
							if (!setLayout(Layout::Concise)) {
								return {};
							}
							break;
						case 'd':
							if (!setLayout(Layout::Detail)) {
								return {};
							}
							break;
						case 'l':
							a_opts.loAgnostic = true;
							break;
						default:
							unknown(token);
							return {};
						}
					}
				}
				rest = after;
			}
		}

		// A form ID as the options ask for it: 00012EB7 / 77495 / 0x012EB7, "Skyrim.esm" / 77495, "Skyrim.esm".
		// Runtime (FF) IDs have no plugin, so -l leaves them as full IDs.
		std::string IdText(RE::FormID a_id, const Options& a_opts)
		{
			if (a_opts.loAgnostic && (a_id >> 24) != 0xFF) {
				if (const auto* owner = LoadOrder::ForFormId(a_id)) {
					const bool light = (a_id >> 24) == 0xFE;
					const auto local = light ? (a_id & 0xFFF) : (a_id & 0xFFFFFF);
					if (a_opts.asInt) {
						return std::format("{}, \"{}\"", local, owner->GetFilename());
					}
					return light ? std::format("0x{:03X}, \"{}\"", local, owner->GetFilename()) :
					               std::format("0x{:06X}, \"{}\"", local, owner->GetFilename());
				}
			}
			return a_opts.asInt ? std::format("{}", a_id) : FormIdText(a_id);
		}

		// Prints result lines: the first kConsoleCap in the console; with a redirect, every line goes to it.
		void EmitCapped(const std::vector<std::string>& a_lines)
		{
			const bool redirecting = ConsoleCommands::IsRedirecting();
			for (std::size_t i = 0; i < a_lines.size(); ++i) {
				if (i < kConsoleCap) {
					Print(a_lines[i]);
				} else if (redirecting) {
					ConsoleCommands::PrintRedirectOnly(a_lines[i]);
				} else {
					break;
				}
			}
		}

		void PrintCapNote(std::size_t a_total, std::string_view a_what)
		{
			if (a_total > kConsoleCap && !ConsoleCommands::IsRedirecting()) {
				Print(std::format("  {} {} match; showing the first {}. Add > name to save them all to a file.", a_total, a_what, kConsoleCap));
			} else if (a_total > kConsoleCap) {
				Print(std::format("  {} {} match; the first {} are shown here, all of them are in the output.", a_total, a_what, kConsoleCap));
			}
		}

		// Aligns rows of cells into columns (header first).
		std::vector<std::string> Columns(const std::vector<std::vector<std::string>>& a_rows)
		{
			std::vector<std::size_t> widths;
			for (const auto& row : a_rows) {
				widths.resize(std::max(widths.size(), row.size()));
				for (std::size_t c = 0; c < row.size(); ++c) {
					widths[c] = std::max(widths[c], row[c].size());
				}
			}
			std::vector<std::string> out;
			for (const auto& row : a_rows) {
				std::string line;
				for (std::size_t c = 0; c < row.size(); ++c) {
					line += row[c];
					if (c + 1 < row.size()) {
						line += std::string(widths[c] - row[c].size() + 2, ' ');
					}
				}
				out.push_back(std::move(line));
			}
			return out;
		}

		// ---------- plugin search ----------

		void PrintPlugin(const RE::TESFile* a_file, std::string_view a_indent = "  ")
		{
			Print(std::format("{}{:<9} {}   (form IDs {})", a_indent, LoadOrder::Slot(a_file), a_file->GetFilename(), LoadOrder::FormIdPattern(a_file)));
		}

		void PrintPlugins(const std::vector<const RE::TESFile*>& a_files, const Options& a_opts)
		{
			switch (a_opts.layout) {
			case Layout::Concise:
				for (const auto* file : a_files) {
					Print(file->GetFilename());
				}
				break;
			case Layout::Detail:
				{
					std::vector<std::vector<std::string>> rows{ { "Slot", "Form IDs", "Light", "Masters", "File" } };
					for (const auto* file : a_files) {
						rows.push_back({ LoadOrder::Slot(file), LoadOrder::FormIdPattern(file), file->IsLight() ? "yes" : "no",
							std::to_string(file->masterCount), std::string{ file->GetFilename() } });
					}
					for (const auto& line : Columns(rows)) {
						Print(line);
					}
					break;
				}
			default:
				for (const auto* file : a_files) {
					PrintPlugin(file);
				}
				break;
			}
		}

		void SearchPlugins(const std::vector<std::vector<Term>>& a_groups, const Options& a_opts)
		{
			const bool chatty = a_opts.layout != Layout::Concise;
			if (chatty) {
				PrintCriteria(a_groups, false);
			}
			const auto all = LoadOrder::AllLoaded();

			// A single unscoped hex-looking term: show slot matches and name matches separately.
			const auto* only = a_groups.size() == 1 && a_groups[0].size() == 1 ? &a_groups[0][0] : nullptr;
			if (a_opts.layout == Layout::Default && only && only->field == Field::Any && !only->negated && LooksLikeSlot(only->value)) {
				const auto& v = only->value;
				std::size_t bySlot = 0;
				std::size_t byName = 0;
				Print(std::format("  Load-order slot starts with {}:", Upper(v)));
				for (auto* file : all) {
					if (LoadOrder::SlotHex(file).starts_with(Upper(v))) {
						PrintPlugin(file, "    ");
						++bySlot;
					}
				}
				if (bySlot == 0) {
					Print("    (no plugin loaded in that slot)");
				}
				Print(std::format("  Name contains \"{}\":", v));
				for (auto* file : all) {
					if (IContains(file->GetFilename(), v)) {
						PrintPlugin(file, "    ");
						++byName;
					}
				}
				if (byName == 0) {
					Print("    (none)");
				}
				return;
			}

			EditorIds                       edids;
			const auto                      matchers = BuildMatchers(a_groups, edids);
			std::vector<const RE::TESFile*> hits;
			for (auto* file : all) {
				const bool hit = std::ranges::any_of(matchers, [&](const std::vector<Matcher>& group) {
					return std::ranges::all_of(group, [&](const Matcher& m) { return MatchesPlugin(m, file); });
				});
				if (hit) {
					hits.push_back(file);
				}
			}
			PrintPlugins(hits, a_opts);
			if (chatty) {
				Print(hits.empty() ? "  No plugins match" : std::format("  {} of {} plugins match", hits.size(), all.size()));
			}
		}

		// ---------- records ----------

		void PrintEditorIdNote()
		{
			Print("    Note: to save memory the game discards most editor IDs after loading.");
			Print("          powerofthree's Tweaks (or Native EditorID Fix) keeps all of them.");
		}

		void PrintFormDetails(RE::TESForm* a_form, EditorIds& a_edids)
		{
			std::string line = std::format("    Record: {} {:08X}", RE::FormTypeToString(a_form->GetFormType()), a_form->GetFormID());
			if (const auto* name = a_form->GetName(); name && *name) {
				line += std::format("  Name: \"{}\"", name);
			}
			Print(line);

			if (const auto edid = a_edids.Get(a_form); !edid.empty()) {
				Print(std::format("    EditorID: {}", edid));
			} else {
				Print("    EditorID: (not kept in memory by the game)");
			}

			const auto id = a_form->GetFormID();
			if ((id >> 24) != 0xFF && LoadOrder::ForFormId(id)) {
				Print(std::format("    LO-agnostic: {}   (as int: {})", IdText(id, { .loAgnostic = true }), IdText(id, { .loAgnostic = true, .asInt = true })));
			}

			if (const auto* kf = a_form->As<RE::BGSKeywordForm>(); kf && kf->GetNumKeywords() > 0) {
				std::string list;
				for (std::uint32_t i = 0; i < kf->GetNumKeywords(); ++i) {
					if (const auto kw = kf->GetKeywordAt(i); kw && *kw) {
						const auto edid = a_edids.Get(*kw);
						list += std::format("{}{}", list.empty() ? "" : ", ", edid.empty() ? FormIdText((*kw)->GetFormID()) : edid);
					}
				}
				Print(std::format("    Keywords ({}): {}", kf->GetNumKeywords(), list));
			}

			const auto* files = a_form->sourceFiles.array;
			if (!files || files->empty()) {
				Print("    No plugin lists this record (created at runtime or by a script)");
				return;
			}
			Print(std::format("    Master: {}", LoadOrder::Describe((*files)[0])));
			Print(std::format("    Last edited by: {}{}", LoadOrder::Describe(files->back()),
				files->size() == 1 ? "  (no other plugin edits it)" : ""));
			if (files->size() > 2) {
				Print(std::format("    All {} plugins with this record, in load order:", files->size()));
				for (auto* file : *files) {
					Print(std::format("      {}", LoadOrder::Describe(file)));
				}
			}
		}

		void LookupFormId(std::string_view a_value)
		{
			EditorIds  edids;
			const auto formId = Query::ParseHex(Query::StripHexPrefix(a_value)).value_or(0);
			Print(std::format("  Form ID {:08X}", formId));

			const auto top = formId >> 24;
			if (top == 0xFF) {
				Print("    Created at runtime (FF prefix): it comes from the save, not from a plugin");
			} else if (auto* owner = LoadOrder::ForFormId(formId)) {
				Print(std::format("    ID belongs to: {}", LoadOrder::Describe(owner)));
			} else if (top == 0xFE) {
				Print(std::format("    ID points at light slot FE {:03X}, but no light plugin is loaded there", (formId >> 12) & 0xFFF));
			} else {
				Print(std::format("    ID points at slot {:02X}, but no plugin is loaded there", top));
			}

			if (auto* form = RE::TESForm::LookupByID(formId)) {
				PrintFormDetails(form, edids);
			} else {
				Print("    No form with this ID exists in the game right now");
			}
		}

		bool GroupHasPositive(const std::vector<Term>& a_group, Field a_field, bool a_allowRegex)
		{
			return std::ranges::any_of(a_group, [&](const Term& t) {
				return t.field == a_field && !t.negated && (a_allowRegex || t.match != Match::Regex);
			});
		}

		std::string NameOf(const RE::TESForm* a_form)
		{
			const auto* name = a_form->GetName();
			return name ? name : "";
		}

		std::string FileText(const RE::TESFile* a_file)
		{
			return a_file ? LoadOrder::Describe(a_file) : "(runtime)";
		}

		void PrintRecords(const std::vector<RE::TESForm*>& a_forms, EditorIds& a_edids, const Options& a_opts)
		{
			// Only build as many lines as will be shown (all of them when redirecting).
			const auto count = ConsoleCommands::IsRedirecting() ? a_forms.size() : std::min(a_forms.size(), kConsoleCap);

			std::vector<std::string> lines;
			if (a_opts.layout == Layout::Detail) {
				std::vector<std::vector<std::string>> rows{ { "Form ID", "Type", "Editor ID", "Name", "Master", "Last edited by" } };
				for (std::size_t i = 0; i < count; ++i) {
					const auto* f = a_forms[i];
					rows.push_back({ IdText(f->GetFormID(), a_opts), std::string{ RE::FormTypeToString(f->GetFormType()) }, a_edids.Get(f),
						NameOf(f), FileText(f->GetFile(0)), FileText(f->GetFile(-1)) });
				}
				lines = Columns(rows);
				Print(lines.front());  // the header row
				lines.erase(lines.begin());
			} else {
				for (std::size_t i = 0; i < count; ++i) {
					const auto* f = a_forms[i];
					if (a_opts.layout == Layout::Concise) {
						lines.push_back(IdText(f->GetFormID(), a_opts));
						continue;
					}
					const auto edid = a_edids.Get(f);
					const auto name = NameOf(f);
					lines.push_back(std::format("  {} {:<4} {}{}   last: {}", IdText(f->GetFormID(), a_opts), RE::FormTypeToString(f->GetFormType()),
						edid.empty() ? "(no editor ID)" : edid, name.empty() ? std::string{} : std::format("  \"{}\"", name), FileText(f->GetFile(-1))));
				}
			}
			EmitCapped(lines);
		}

		void SearchRecords(const std::vector<std::vector<Term>>& a_groups, const Options& a_opts)
		{
			const bool chatty = a_opts.layout != Layout::Concise;
			if (chatty) {
				PrintCriteria(a_groups, true);
			}
			EditorIds  edids;
			const auto matchers = BuildMatchers(a_groups, edids);

			// Candidates come from the game's form-ID table, which only holds live forms. Never take
			// forms from the editor-ID map: it keeps stale pointers to forms discarded during loading
			// (reading one crashed the game), so it is only used to look up names by pointer.
			std::vector<RE::TESForm*> candidates;
			const bool byFormId = std::ranges::all_of(a_groups, [](const auto& g) { return GroupHasPositive(g, Field::FormId, false); });
			if (byFormId) {
				for (const auto& t : std::views::join(a_groups)) {
					if (t.field == Field::FormId && !t.negated && t.match != Match::Regex) {
						if (auto* form = RE::TESForm::LookupByID(Query::ParseHex(Query::StripHexPrefix(t.value)).value_or(0))) {
							candidates.push_back(form);
						}
					}
				}
			} else {
				const auto& [map, lock] = RE::TESForm::GetAllForms();
				const RE::BSReadLockGuard guard{ lock };
				if (map) {
					candidates.reserve(map->size());
					for (const auto& [id, form] : *map) {
						if (form) {
							candidates.push_back(form);
						}
					}
				}
			}

			std::vector<RE::TESForm*>              results;
			std::unordered_set<const RE::TESForm*> seen;
			for (auto* form : candidates) {
				if (seen.contains(form)) {
					continue;
				}
				const bool hit = std::ranges::any_of(matchers, [&](const std::vector<Matcher>& group) {
					return std::ranges::all_of(group, [&](const Matcher& m) { return MatchesRecord(m, form, edids); });
				});
				if (hit) {
					seen.insert(form);
					results.push_back(form);
				}
			}
			std::ranges::sort(results, {}, [](const RE::TESForm* f) { return f->GetFormID(); });

			if (results.empty()) {
				Print("  No records match");
				if (chatty && std::ranges::any_of(a_groups, [](const auto& g) { return GroupHasPositive(g, Field::EditorId, true); })) {
					PrintEditorIdNote();
				}
				return;
			}
			if (results.size() == 1 && a_opts.layout == Layout::Default) {
				Print("  1 record matches:");
				PrintFormDetails(results[0], edids);
				return;
			}

			PrintRecords(results, edids, a_opts);
			PrintCapNote(results.size(), "records");
			if (chatty && results.size() <= kConsoleCap) {
				Print(std::format("  {} records match", results.size()));
			}
		}

		// ---------- entry point ----------

		void PrintHelp()
		{
			Print("WhichPlugin (wp) v" PLUGIN_VERSION_STRING " - find loaded plugins, load-order slots and records");
			Print("  wp                              list every loaded plugin");
			Print("  wp patch                        plugin name contains, or slot starts with (if hex)");
			Print("  wp FE                           light plugins + names containing \"fe\"");
			Print("  wp 00012EB7                     which plugin owns a form ID; master, last edit, editor ID");
			Print("Combine:  && = AND   || = OR (AND binds first)   ! = NOT   e.g. wp \"cc\" && !\"esl\"");
			Print("Fields and how they match  (: contains   = exact   ~ regex):");
			Print("  plugin-name:dawn                plugin name / for records: last plugin to edit it");
			Print("  plugin-master:\"Dawnguard\"       plugin that has it as a master / for records: the master");
			Print("  lobytes:FE0                     slot / form ID starts with (FE0 = FE000-FE0FF)");
			Print("  formid:12EB7   formid~\"^0A\"     a form ID (leading zeros optional)");
			Print("  edid:iron   edid=IronSword   edid~\"^Iron.*Sword$\"");
			Print("  type=WEAP   type~\"^(ARMO|WEAP)$\"   record type");
			Print("  keyword:WeapTypeSword   keyword:0001E711   keyword~\"^WeapType\"");
			Print("Using formid, edid, type or keyword searches records; plugin fields then filter them.");
			Print("  e.g. wp edid~\"^Iron\" && plugin-master:dawnguard");
			Print("Options (first on the line):  -c/--concise  -d/--detail  -l/--lo-agnostic  --int   (-- ends options)");
			Print("  -c  IDs only      -d  one aligned row each      -l  0x00E8F0, \"Dawnguard.esm\"      --int  decimal");
			Print("Quotes: \"...\" is taken literally. In '...' write \\' for an apostrophe.");
			Print("Output (at the end of the line):");
			Print("  > name     write to My Games\\Skyrim Special Edition\\name(.txt), replacing it");
			Print("  >> name    append to that file;  >> alone appends to the last file used");
			Print("  >>>        copy the output to the clipboard");
			Print("  |          pipe into text tools (grep, sed, sort... coming soon)");
		}

		void Run(std::string_view a_args)
		{
			Options     opts;
			std::string optionError;
			const auto  query = ParseOptions(a_args, opts, optionError);
			if (!optionError.empty()) {
				Print(std::format("{} {}", LONG_NAME, a_args));
				Print(std::format("  Error: {}", optionError));
				return;
			}

			auto       parsed = Query::Parse(query);
			const auto all = LoadOrder::AllLoaded();

			if (parsed.terms.empty() && parsed.errors.empty()) {
				if (opts.layout != Layout::Concise) {
					Print(std::format("{}: all {} loaded plugins", LONG_NAME, all.size()));
				}
				PrintPlugins(all, opts);
				return;
			}

			if (parsed.terms.size() == 1 && parsed.terms[0].field == Field::Any &&
				(Lower(parsed.terms[0].value) == "help" || parsed.terms[0].value == "?")) {
				PrintHelp();
				return;
			}

			const bool chatty = opts.layout != Layout::Concise;
			if (chatty) {
				Print(std::format("{} {}", LONG_NAME, a_args));
			}
			for (const auto& w : parsed.warnings) {
				Print(std::format("  Warning: {}", w));
			}
			if (!parsed.errors.empty()) {
				for (const auto& e : parsed.errors) {
					Print(std::format("  Error: {}", e));
				}
				Print("  Type 'wp help' for the syntax.");
				return;
			}

			// A bare 8-digit hex value (or 0x...) is a form ID.
			for (auto& group : parsed.groups) {
				for (auto& t : group) {
					if (t.field == Field::Any && LooksLikeFormId(t.value)) {
						t.field = Field::FormId;
						t.match = Match::Contains;
					}
				}
			}

			// A single form ID gets the detailed report, even if the form doesn't exist (default layout only).
			if (opts.layout == Layout::Default && parsed.groups.size() == 1 && parsed.groups[0].size() == 1) {
				const auto& t = parsed.groups[0][0];
				if (t.field == Field::FormId && !t.negated && t.match != Match::Regex) {
					LookupFormId(t.value);
					return;
				}
			}

			// Keyword terms must name keywords that exist; say so before searching.
			{
				EditorIds edids;
				bool      bad = false;
				for (const auto& t : std::views::join(parsed.groups)) {
					if (t.field == Field::Keyword) {
						std::string error;
						if (ResolveKeywords(t, edids, error).empty()) {
							Print(std::format("  Error: {}", error));
							bad = true;
						}
					}
				}
				if (bad) {
					return;
				}
			}

			const bool records = std::ranges::any_of(std::views::join(parsed.groups), [](const Term& t) { return Query::IsRecordField(t.field); });
			if (records) {
				SearchRecords(parsed.groups, opts);
			} else {
				SearchPlugins(parsed.groups, opts);
			}
		}

		// Fallback when the console hook isn't installed: the game's parser hands us one argument.
		bool ExecuteFromSlot(const RE::SCRIPT_PARAMETER*, RE::SCRIPT_FUNCTION::ScriptData* a_scriptData, RE::TESObjectREFR*, RE::TESObjectREFR*, RE::Script*, RE::ScriptLocals*, double&, std::uint32_t&)
		{
			const auto arg = ConsoleCommands::GetStringArg(a_scriptData);
			Run(arg ? std::string_view{ *arg } : std::string_view{});
			return true;
		}
	}

	void Register()
	{
		ConsoleHook::AddCommand(LONG_NAME, SHORT_NAME, &Run);

		static RE::SCRIPT_PARAMETER params[] = {
			{ "Search (wp help for syntax)", RE::SCRIPT_PARAM_TYPE::kChar, true },
		};
		ConsoleCommands::Register({
			.longName = LONG_NAME,
			.shortName = SHORT_NAME,
			.help = "Find loaded plugins, load-order slots and records. Type: wp help",
			.params = params,
			.numParams = static_cast<std::uint16_t>(std::size(params)),
			.execute = &ExecuteFromSlot,
		});
	}
}
