#include "LoadOrder.h"

namespace LoadOrder
{
	std::string Slot(const RE::TESFile* a_file)
	{
		if (!a_file) {
			return "[??]";
		}
		if (a_file->IsLight()) {
			return std::format("[FE {:03X}]", a_file->GetSmallFileCompileIndex());
		}
		return std::format("[{:02X}]", a_file->GetCompileIndex());
	}

	std::string SlotHex(const RE::TESFile* a_file)
	{
		if (!a_file) {
			return "";
		}
		if (a_file->IsLight()) {
			return std::format("FE{:03X}", a_file->GetSmallFileCompileIndex());
		}
		return std::format("{:02X}", a_file->GetCompileIndex());
	}

	std::string FormIdPattern(const RE::TESFile* a_file)
	{
		if (!a_file) {
			return "";
		}
		if (a_file->IsLight()) {
			return std::format("FE{:03X}xxx", a_file->GetSmallFileCompileIndex());
		}
		return std::format("{:02X}xxxxxx", a_file->GetCompileIndex());
	}

	std::string Describe(const RE::TESFile* a_file)
	{
		if (!a_file) {
			return "<unknown plugin>";
		}
		return std::format("{} {}", Slot(a_file), a_file->GetFilename());
	}

	std::vector<const RE::TESFile*> AllLoaded()
	{
		std::vector<const RE::TESFile*> out;
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return out;
		}
		for (auto* file : data->compiledFileCollection.files) {
			out.push_back(file);
		}
		for (auto* file : data->compiledFileCollection.smallFiles) {
			out.push_back(file);
		}
		return out;
	}

	const RE::TESFile* ByFullIndex(std::uint8_t a_index)
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		return data ? data->LookupLoadedModByIndex(a_index) : nullptr;
	}

	const RE::TESFile* ByLightIndex(std::uint16_t a_index)
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		return data ? data->LookupLoadedLightModByIndex(a_index) : nullptr;
	}

	const RE::TESFile* ForFormId(RE::FormID a_formId)
	{
		const auto top = static_cast<std::uint8_t>(a_formId >> 24);
		if (top == 0xFF) {
			return nullptr;
		}
		if (top == 0xFE) {
			return ByLightIndex(static_cast<std::uint16_t>((a_formId >> 12) & 0xFFF));
		}
		return ByFullIndex(top);
	}
}
