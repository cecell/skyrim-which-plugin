#include "ConsoleHook.h"
#include "Settings.h"
#include "WhichPlugin.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostPostLoad:
			// After every plugin's kPostLoad, so our hook is the outermost (see ConsoleHook.h).
			ConsoleHook::Install();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			WhichPlugin::Register();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// The settings come first: they set the log level.
	std::vector<std::string> problems;
	Settings::Load(problems);

	// CommonLib opens Documents\My Games\Skyrim Special Edition\SKSE\WhichPlugin.log itself.
	SKSE::Init(a_skse, { .logLevel = Settings::Get().logLevel, .trampoline = true, .trampolineSize = 64 });
	SKSE::log::info("WhichPlugin loaded, game version {}", REL::Module::get().version().string());
	for (const auto& problem : problems) {
		SKSE::log::warn("WhichPlugin.ini: {}", problem);
	}

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		SKSE::log::critical("Failed to register the SKSE message listener");
		return false;
	}
	return true;
}
