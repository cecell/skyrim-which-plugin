#include "ConsoleCommands.h"

namespace ConsoleCommands
{
	namespace
	{
		// Leftover PS3 debug commands (SPU = PS3 Cell co-processors, RSX = PS3 GPU). They do
		// nothing on PC, so they are safe to repurpose.
		constexpr std::array kSpareSlots{
			"ToggleSPURenderBatch"sv,
			"ToggleSPUTransformUpdate"sv,
			"ToggleSPUCulling"sv,
			"ForceRSXCrash"sv,
		};

		bool NameMatches(const char* a_name, std::string_view a_wanted)
		{
			return a_name && a_wanted.size() == std::strlen(a_name) &&
			       _strnicmp(a_name, a_wanted.data(), a_wanted.size()) == 0;
		}

		// True if any console or script command already answers to this name (long or short).
		bool NameInUse(std::string_view a_name)
		{
			auto check = [&](RE::SCRIPT_FUNCTION* a_first, std::uint16_t a_count) {
				for (std::uint16_t i = 0; i < a_count; ++i) {
					if (NameMatches(a_first[i].functionName, a_name) || NameMatches(a_first[i].shortName, a_name)) {
						return true;
					}
				}
				return false;
			};
			using Commands = RE::SCRIPT_FUNCTION::Commands;
			return check(RE::SCRIPT_FUNCTION::GetFirstConsoleCommand(), Commands::kConsoleCommandsEnd) ||
			       check(RE::SCRIPT_FUNCTION::GetFirstScriptCommand(), Commands::kScriptCommandsEnd);
		}
	}

	bool Register(const Definition& a_def)
	{
		for (auto name : { std::string_view{ a_def.longName }, std::string_view{ a_def.shortName } }) {
			if (!name.empty() && NameInUse(name)) {
				SKSE::log::error("Console command '{}' not registered: the name '{}' is already used by another command", a_def.longName, name);
				return false;
			}
		}

		for (auto slotName : kSpareSlots) {
			auto* slot = RE::SCRIPT_FUNCTION::LocateConsoleCommand(slotName);
			if (!slot) {
				continue;  // renamed already, i.e. taken by another mod (or by us)
			}
			slot->functionName = a_def.longName;
			slot->shortName = a_def.shortName;
			slot->helpString = a_def.help;
			slot->referenceFunction = false;
			slot->numParams = a_def.numParams;
			slot->params = a_def.params;
			slot->executeFunction = a_def.execute;
			slot->conditionFunction = nullptr;
			SKSE::log::info("Registered console command {} ({}) in place of unused '{}'", a_def.longName, a_def.shortName, slotName);
			return true;
		}

		SKSE::log::error("Console command '{}' not registered: every spare slot is already taken by other mods", a_def.longName);
		return false;
	}

	namespace
	{
		std::vector<std::string>* g_capture = nullptr;
		bool                      g_redirecting = false;
	}

	Capture::Capture(bool a_redirecting) :
		_previous(g_capture),
		_previousRedirecting(g_redirecting)
	{
		g_capture = &_lines;
		g_redirecting = a_redirecting;
	}

	Capture::~Capture()
	{
		g_capture = _previous;
		g_redirecting = _previousRedirecting;
	}

	bool IsRedirecting()
	{
		return g_redirecting;
	}

	void PrintRedirectOnly(std::string_view a_line)
	{
		if (g_capture && g_redirecting) {
			g_capture->emplace_back(a_line);
		}
	}

	void Print(std::string_view a_line)
	{
		if (g_capture) {
			g_capture->emplace_back(a_line);
		}
		if (auto* console = RE::ConsoleLog::GetSingleton()) {
			const std::string line{ a_line };
			console->Print("%s", line.c_str());
		}
	}

	std::optional<std::string> GetStringArg(RE::SCRIPT_FUNCTION::ScriptData* a_scriptData)
	{
		if (!a_scriptData || a_scriptData->numParams == 0) {
			return std::nullopt;
		}
		auto* chunk = a_scriptData->GetStringChunk();
		if (!chunk) {
			return std::nullopt;
		}
		auto text = chunk->GetString();
		if (text.empty()) {
			return std::nullopt;
		}
		return text;
	}
}
