#include "WhichPlugin.h"

#include "ConsoleCommands.h"
#include "ConsoleHook.h"
#include "LoadOrder.h"
#include "Open.h"
#include "Query.h"
#include "Redirect.h"
#include "Settings.h"

#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace WhichPlugin
{
	namespace
	{
		constexpr auto        LONG_NAME = "WhichPlugin";
		constexpr auto README_URL = "https://github.com/cecell/skyrim-which-plugin#readme";

		// Result lines shown in the console (iMaxConsoleLines); a redirect gets all of them.
		std::size_t ConsoleCap() { return Settings::Get().maxConsoleLines; }

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

		// The in-game name. A reference has none of its own: it shows its base record's.
		std::string NameOf(const RE::TESForm* a_form)
		{
			const auto* ref = a_form->AsReference();
			const auto* name = ref ? ref->GetName() : a_form->GetName();
			return name ? name : "";
		}

		bool IsList(const RE::TESForm* a_form)
		{
			return a_form->As<RE::TESLeveledList>() || a_form->Is(RE::FormType::FormList);
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

			std::vector<const RE::BGSKeyword*>     keywords;  // keyword terms: the keywords it refers to
			std::unordered_set<const RE::TESForm*> targets;   // refs/base/contents terms: see ResolveTargets
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

		// What a refs:, base: or contents: term compares records against. ':' and '=' name one record by
		// form ID or editor ID, '~' every record whose editor ID matches. The result is
		//   refs:      those base records (a reference matches if its base is one of them),
		//   base:      the base records of those references,
		//   contents:  every entry of those leveled lists and form lists, nested lists and their
		//              entries included.
		// Candidates come from the game's form-ID table (live forms only). Empty result: a_error says why.
		std::unordered_set<const RE::TESForm*> ResolveTargets(const Term& a_term, EditorIds& a_edids, std::string& a_error)
		{
			const auto fits = [&](const RE::TESForm* a_form) {
				switch (a_term.field) {
				case Field::Refs:
					return a_form->AsReference() == nullptr;
				case Field::Base:
					return a_form->AsReference() != nullptr;
				default:
					return IsList(a_form);
				}
			};
			const auto* what = a_term.field == Field::Refs ? "base record" : a_term.field == Field::Base ? "reference" : "leveled list or form list";

			std::vector<const RE::TESForm*> named;
			const auto                      digits = Query::StripHexPrefix(a_term.value);
			if (a_term.match != Match::Regex && Query::IsHex(digits) && digits.size() <= 8) {
				if (const auto* form = RE::TESForm::LookupByID(Query::ParseHex(digits).value_or(0)); form && fits(form)) {
					named.push_back(form);
				}
			}
			if (named.empty()) {
				std::optional<std::regex> re;
				if (a_term.match == Match::Regex) {
					re.emplace(a_term.value, std::regex::ECMAScript | std::regex::icase);
				}
				const auto  wanted = Lower(a_term.value);
				const auto& [map, lock] = RE::TESForm::GetAllForms();
				const RE::BSReadLockGuard guard{ lock };
				if (map) {
					for (const auto& [id, form] : *map) {
						if (!form || !fits(form)) {
							continue;
						}
						const auto edid = a_edids.Get(form);
						if (!edid.empty() && (re ? std::regex_search(edid, *re) : Lower(edid) == wanted)) {
							named.push_back(form);
						}
					}
				}
			}

			std::unordered_set<const RE::TESForm*> out;
			if (named.empty()) {
				a_error = a_term.match == Match::Regex ?
				              std::format("{} - no {}'s editor ID matches", a_term.source, what) :
				              std::format("{} - no {} has that form ID or editor ID", a_term.source, what);
				return out;
			}
			switch (a_term.field) {
			case Field::Refs:
				out.insert(named.begin(), named.end());
				break;
			case Field::Base:
				for (const auto* ref : named) {
					if (const auto* base = ref->AsReference()->GetBaseObject()) {
						out.insert(base);
					}
				}
				break;
			default:
				{
					std::vector<const RE::TESForm*>        pending(named.begin(), named.end());
					std::unordered_set<const RE::TESForm*> opened;  // lists can contain each other
					const auto                             add = [&](const RE::TESForm* a_entry) {
                        if (a_entry) {
                            out.insert(a_entry);
                            if (IsList(a_entry)) {
                                pending.push_back(a_entry);
                            }
                        }
					};
					while (!pending.empty()) {
						const auto* list = pending.back();
						pending.pop_back();
						if (!opened.insert(list).second) {
							continue;
						}
						if (const auto* leveled = list->As<RE::TESLeveledList>()) {
							for (const auto& entry : leveled->entries) {
								add(entry.form);
							}
						} else if (const auto* formList = list->As<RE::BGSListForm>()) {
							for (const auto* entry : formList->forms) {
								add(entry);
							}
							if (formList->scriptAddedTempForms) {
								for (const auto id : *formList->scriptAddedTempForms) {
									add(RE::TESForm::LookupByID(id));
								}
							}
						}
					}
					break;
				}
			}
			return out;
		}

		std::vector<std::vector<Matcher>> BuildMatchers(const std::vector<std::vector<Term>>& a_groups, EditorIds& a_edids)
		{
			std::vector<std::vector<Matcher>> out;
			for (const auto& group : a_groups) {
				auto& g = out.emplace_back();
				for (const auto& t : group) {
					auto&       m = g.emplace_back(t);
					std::string unused;
					if (t.field == Field::Keyword) {
						m.keywords = ResolveKeywords(t, a_edids, unused);
					} else if (Query::IsLookupField(t.field)) {
						m.targets = ResolveTargets(t, a_edids, unused);
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
			case Field::Name:
				hit = a_m.Test(NameOf(a_form));
				break;
			case Field::Refs:
				if (const auto* ref = a_form->AsReference()) {
					hit = a_m.targets.contains(ref->GetBaseObject());
				}
				break;
			case Field::Base:
			case Field::Contents:
				hit = a_m.targets.contains(a_form);
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
			case Field::Name:
				subject = "in-game name";
				break;
			case Field::Refs:
			case Field::Base:
			case Field::Contents:
				{
					const bool  regex = a_term.match == Match::Regex;
					std::string what;
					if (a_term.field == Field::Refs) {
						what = regex ? std::format("is a reference to a record whose editor ID matches /{}/", v) : std::format("is a reference to {}", v);
					} else if (a_term.field == Field::Base) {
						what = regex ? std::format("is the base record of a reference whose editor ID matches /{}/", v) : std::format("is the base record of reference {}", v);
					} else {
						what = regex ? std::format("is in a list whose editor ID matches /{}/ (nested lists included)", v) : std::format("is in list {} (nested lists included)", v);
					}
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

		// Default, Concise (-c: just the IDs or file names) or Detail (-d: one aligned row per result,
		// like `ls -l`). The default comes from sDefaultLayout.
		using Layout = Settings::Layout;

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

		// Prints result lines: the first ConsoleCap() in the console; with a redirect, every line goes to it.
		void EmitCapped(const std::vector<std::string>& a_lines)
		{
			const bool redirecting = ConsoleCommands::IsRedirecting();
			for (std::size_t i = 0; i < a_lines.size(); ++i) {
				if (i < ConsoleCap()) {
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
			if (a_total > ConsoleCap() && !ConsoleCommands::IsRedirecting()) {
				Print(std::format("  {} {} match; showing the first {}. Add > name to save them all to a file.", a_total, a_what, ConsoleCap()));
			} else if (a_total > ConsoleCap()) {
				Print(std::format("  {} {} match; the first {} are shown here, all of them are in the output.", a_total, a_what, ConsoleCap()));
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
			if (const auto name = NameOf(a_form); !name.empty()) {
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

			// "TYPE 00012345 EditorID "Name"", for the records a reference points at.
			const auto label = [&](const RE::TESForm* a_other) {
				const auto edid = a_edids.Get(a_other);
				const auto name = NameOf(a_other);
				return std::format("{} {:08X}{}{}", RE::FormTypeToString(a_other->GetFormType()), a_other->GetFormID(),
					edid.empty() ? "" : " " + edid, name.empty() ? "" : std::format(" \"{}\"", name));
			};
			if (const auto* ref = a_form->AsReference()) {
				if (const auto* base = ref->GetBaseObject()) {
					Print(std::format("    Base: {}", label(base)));
				}
				if (const auto* cell = ref->GetParentCell()) {
					Print(std::format("    Cell: {} ({}){}", label(cell), cell->IsInteriorCell() ? "interior" : "exterior",
						a_form->IsDisabled() ? "  - this reference is disabled" : ""));
				}
				const auto pos = ref->GetPosition();
				Print(std::format("    Position: {:.0f}, {:.0f}, {:.0f}", pos.x, pos.y, pos.z));
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

		std::string FileText(const RE::TESFile* a_file)
		{
			return a_file ? LoadOrder::Describe(a_file) : "(runtime)";
		}

		void PrintRecords(const std::vector<RE::TESForm*>& a_forms, EditorIds& a_edids, const Options& a_opts)
		{
			// Only build as many lines as will be shown (all of them when redirecting).
			const auto count = ConsoleCommands::IsRedirecting() ? a_forms.size() : std::min(a_forms.size(), ConsoleCap());

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

			// Only references in memory exist to be found; say so whenever a search asks for them.
			const bool refsNote = chatty && std::ranges::any_of(std::views::join(a_groups), [](const Term& t) { return t.field == Field::Refs && !t.negated; });
			const auto printRefsNote = [&] {
				if (refsNote) {
					Print("  Only references in memory are searched: the loaded cells and persistent references.");
				}
			};

			if (results.empty()) {
				Print("  No records match");
				if (chatty && std::ranges::any_of(a_groups, [](const auto& g) { return GroupHasPositive(g, Field::EditorId, true); })) {
					PrintEditorIdNote();
				}
				printRefsNote();
				return;
			}
			if (results.size() > 1 || a_opts.layout != Layout::Default) {
				printRefsNote();
			}
			if (results.size() == 1 && a_opts.layout == Layout::Default) {
				Print("  1 record matches:");
				PrintFormDetails(results[0], edids);
				return;
			}

			PrintRecords(results, edids, a_opts);
			PrintCapNote(results.size(), "records");
			if (chatty && results.size() <= ConsoleCap()) {
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
			Print("  name:\"iron sword\"   name~\"^Daedric\"   in-game name (works without editor IDs)");
			Print("  refs:IronSword                  placed references of a record (loaded cells only)");
			Print("  base:00000014                   the base record of a reference (14 = the player)");
			Print("  contents:LItemWeaponSword       what a leveled list or form list holds, nested lists too");
			Print("Using formid, edid, name, type, keyword, refs, base or contents searches records;");
			Print("  plugin fields then filter them.");
			Print("  e.g. wp edid~\"^Iron\" && plugin-master:dawnguard");
			Print("Options (first on the line):  -c/--concise  -d/--detail  -l/--lo-agnostic  --int   (-- ends options)");
			Print("  -c  IDs only      -d  one aligned row each      -l  0x00E8F0, \"Dawnguard.esm\"      --int  decimal");
			Print("Quotes: \"...\" is taken literally. In '...' write \\' for an apostrophe.");
			Print("Output (at the end of the line):");
			Print("  > name     write to My Games\\Skyrim Special Edition\\wp\\name(.txt), text file types only");
			Print("  >! name    replace the file even when bOverwriteNeedsConfirm is on");
			Print("  >> name    append to that file;  >> alone appends to the last file used");
			Print("  >>>        copy the output to the clipboard");
			Print("  |          pipe into text tools (grep, sed, sort... coming soon)");
			Print("Actions:");
			Print("  wp open          the output folder           wp open last   the last file written");
			Print("  wp help --web    this help on GitHub         wp json        a JSON checker in the browser");
			Print("  wp uesp WEAP     UESP's page on a record type (or give a form ID)");
			Print("  To search for plugins named like an action, quote it: wp \"open\"");
			Print("Settings: Data\\SKSE\\Plugins\\WhichPlugin.ini");
		}

		// wp open, wp open last, wp help --web, wp json, wp uesp. Returns false if a_args isn't an
		// action (quoted words never are, so wp "open" still searches).
		bool RunAction(std::string_view a_args)
		{
			std::vector<std::string> words;
			for (const auto word : std::views::split(a_args, ' ')) {
				if (!word.empty()) {
					words.emplace_back(Lower(std::string_view{ word.begin(), word.end() }));
				}
			}
			if (words.empty()) {
				return false;
			}
			const auto& action = words[0];
			const auto  report = [](std::string_view a_what, const std::string& a_error) {
                Print(a_error.empty() ? std::format("  Opened {}", a_what) : std::format("  Couldn't open {}: {}", a_what, a_error));
			};
			const auto url = [&](std::string_view a_what, const std::string& a_url) {
				if (!Settings::Get().allowOpenURL) {
					Print("  Opening web pages is off (bAllowOpenURL in WhichPlugin.ini)");
				} else {
					report(a_what, Open::Url(a_url));
				}
			};

			if (action == "help" && words.size() == 2 && (words[1] == "--web" || words[1] == "web")) {
				url(README_URL, README_URL);
				return true;
			}
			if (action == "json" && words.size() == 1) {
				url("the JSON checker", "https://jsonlint.com/");
				return true;
			}
			if (action == "uesp" && words.size() <= 2) {
				std::string type;
				if (words.size() == 2) {
					const auto digits = Query::StripHexPrefix(words[1]);
					const auto* form = Query::IsHex(digits) && digits.size() <= 8 ? RE::TESForm::LookupByID(Query::ParseHex(digits).value_or(0)) : nullptr;
					type = form ? std::string{ RE::FormTypeToString(form->GetFormType()) } : Upper(words[1]);
					if (type.size() != 4 || !std::ranges::all_of(type, [](unsigned char c) { return std::isalnum(c) || c == '_'; })) {
						Print(std::format("  {} isn't a record type (like WEAP or NPC_) or the form ID of a loaded record", words[1]));
						return true;
					}
				}
				const auto page = std::format("https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format{}", type.empty() ? "" : "/" + type);
				url(page, page);
				return true;
			}
			if (action == "open" && words.size() <= 2) {
				if (!Settings::Get().allowOpenFolders) {
					Print("  Opening folders and files is off (bAllowOpenFolders in WhichPlugin.ini)");
					return true;
				}
				if (words.size() == 1) {
					if (const auto folder = Redirect::OutputFolder()) {
						report(folder->string(), Open::Folder(*folder));
					} else {
						Print("  Couldn't find the My Games\\Skyrim Special Edition folder");
					}
				} else if (words[1] == "last") {
					const auto& last = Redirect::LastFile();
					if (last.empty()) {
						Print("  No file has been written yet this session (e.g. wp patch > patches)");
					} else if (!Redirect::AllowedExtension(last)) {
						Print(std::format("  {} isn't a text file, so it isn't opened", last.string()));
					} else {
						report(last.string(), Open::File(last));
					}
				} else {
					Print("  wp open: the output folder;  wp open last: the last file written.  To search for plugins named \"open\", type wp \"open\"");
				}
				return true;
			}
			return false;
		}

		void Run(std::string_view a_args)
		{
			if (RunAction(a_args)) {
				return;
			}

			Options     opts{ .layout = Settings::Get().defaultLayout };
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

			// Keyword, refs, base and contents terms must name records that exist; say so before searching.
			{
				EditorIds edids;
				bool      bad = false;
				for (const auto& t : std::views::join(parsed.groups)) {
					std::string error;
					if (t.field == Field::Keyword) {
						ResolveKeywords(t, edids, error);
					} else if (Query::IsLookupField(t.field)) {
						ResolveTargets(t, edids, error);
					}
					if (!error.empty()) {
						Print(std::format("  Error: {}", error));
						bad = true;
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
		ConsoleHook::AddCommand(LONG_NAME, Settings::Get().shortCommand, &Run);

		static RE::SCRIPT_PARAMETER params[] = {
			{ "Search (wp help for syntax)", RE::SCRIPT_PARAM_TYPE::kChar, true },
		};
		ConsoleCommands::Register({
			.longName = LONG_NAME,
			.shortName = Settings::Get().shortCommand.c_str(),
			.help = "Find loaded plugins, load-order slots and records. Type: wp help",
			.params = params,
			.numParams = static_cast<std::uint16_t>(std::size(params)),
			.execute = &ExecuteFromSlot,
		});
	}
}
