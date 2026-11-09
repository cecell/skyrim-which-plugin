#pragma once

// Load-order helpers, formatted the way xEdit shows them.
namespace LoadOrder
{
	// "[0A]" for a full plugin, "[FE 012]" for a light plugin.
	std::string Slot(const RE::TESFile* a_file);

	// The slot as bare hex, for prefix searches: "0A" or "FE012".
	std::string SlotHex(const RE::TESFile* a_file);

	// Form-ID prefix the plugin's forms use: "0Axxxxxx" or "FE012xxx".
	std::string FormIdPattern(const RE::TESFile* a_file);

	// "[0A] Plugin.esp"
	std::string Describe(const RE::TESFile* a_file);

	// Every loaded plugin: full plugins in load order, then light plugins in load order.
	std::vector<const RE::TESFile*> AllLoaded();

	const RE::TESFile* ByFullIndex(std::uint8_t a_index);
	const RE::TESFile* ByLightIndex(std::uint16_t a_index);

	// The plugin a form ID's prefix points at (nullptr for FF runtime IDs or empty slots).
	const RE::TESFile* ForFormId(RE::FormID a_formId);
}
