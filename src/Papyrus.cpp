#include "Papyrus.h"

#include "LoadOrder.h"
#include "Variables.h"
#include "WhichPlugin.h"

namespace Papyrus
{
	namespace
	{
		constexpr auto        kScript = "WhichPlugin";
		constexpr std::int32_t kMaxResults = 10000;

		std::mutex  g_errorLock;
		std::string g_lastError;

		void SetError(std::string a_error)
		{
			const std::scoped_lock lock{ g_errorLock };
			g_lastError = std::move(a_error);
		}

		RE::BSFixedString GetLastError(RE::StaticFunctionTag*)
		{
			const std::scoped_lock lock{ g_errorLock };
			return g_lastError;
		}

		std::vector<RE::TESForm*> Find(RE::StaticFunctionTag*, RE::BSFixedString a_query, std::int32_t a_max)
		{
			std::string error;
			auto        forms = WhichPlugin::FindRecords(a_query.c_str(), error);
			const auto  max = static_cast<std::size_t>(std::clamp(a_max, 1, kMaxResults));
			if (forms.size() > max) {
				forms.resize(max);
			}
			SetError(error);
			return forms;
		}

		std::vector<RE::BSFixedString> FindPlugins(RE::StaticFunctionTag*, RE::BSFixedString a_query)
		{
			std::string                    error;
			std::vector<RE::BSFixedString> names;
			for (const auto* file : WhichPlugin::FindPlugins(a_query.c_str(), error)) {
				names.emplace_back(file->GetFilename());
			}
			SetError(error);
			return names;
		}

		RE::BSFixedString FileName(const RE::TESFile* a_file)
		{
			return a_file ? RE::BSFixedString{ a_file->GetFilename() } : RE::BSFixedString{ "" };
		}

		RE::BSFixedString GetMasterPlugin(RE::StaticFunctionTag*, RE::TESForm* a_form)
		{
			return a_form ? FileName(a_form->GetFile(0)) : "";
		}

		RE::BSFixedString GetLastEditingPlugin(RE::StaticFunctionTag*, RE::TESForm* a_form)
		{
			return a_form ? FileName(a_form->GetFile(-1)) : "";
		}

		std::vector<RE::BSFixedString> GetEditingPlugins(RE::StaticFunctionTag*, RE::TESForm* a_form)
		{
			std::vector<RE::BSFixedString> names;
			if (const auto* files = a_form ? a_form->sourceFiles.array : nullptr) {
				for (const auto* file : *files) {
					names.push_back(FileName(file));
				}
			}
			return names;
		}

		std::int32_t GetLocalFormID(RE::StaticFunctionTag*, RE::TESForm* a_form)
		{
			if (!a_form || !LoadOrder::ForFormId(a_form->GetFormID())) {
				return -1;
			}
			const auto id = a_form->GetFormID();
			return static_cast<std::int32_t>((id >> 24) == 0xFE ? id & 0xFFF : id & 0xFFFFFF);
		}

		RE::BSFixedString GetOwnerPlugin(RE::StaticFunctionTag*, RE::TESForm* a_form)
		{
			return a_form ? FileName(LoadOrder::ForFormId(a_form->GetFormID())) : "";
		}

		std::vector<RE::TESObjectREFR*> GetReferences(RE::StaticFunctionTag*, RE::TESForm* a_base)
		{
			return WhichPlugin::References(a_base);
		}

		std::vector<RE::TESForm*> GetListContents(RE::StaticFunctionTag*, RE::TESForm* a_list, bool a_nested)
		{
			return WhichPlugin::ListContents(a_list, a_nested);
		}

		RE::BSFixedString GetVariable(RE::StaticFunctionTag*, RE::BSFixedString a_name)
		{
			std::string error;
			const auto  value = Variables::Get(a_name.c_str(), error);
			SetError(error);
			return value ? RE::BSFixedString{ *value } : RE::BSFixedString{ "" };
		}

		bool SetVariable(RE::StaticFunctionTag*, RE::BSFixedString a_name, RE::BSFixedString a_value)
		{
			std::string error;
			const bool  ok = Variables::Set(a_name.c_str(), a_value.c_str(), error);
			SetError(error);
			return ok;
		}

		bool UnsetVariable(RE::StaticFunctionTag*, RE::BSFixedString a_name)
		{
			std::string error;
			const bool  ok = Variables::Unset(a_name.c_str(), error);
			SetError(error);
			return ok;
		}

		RE::BSFixedString GetVersion(RE::StaticFunctionTag*)
		{
			return PLUGIN_VERSION_STRING;
		}

		bool Bind(RE::BSScript::IVirtualMachine* a_vm)
		{
			a_vm->RegisterFunction("Find", kScript, Find);
			a_vm->RegisterFunction("FindPlugins", kScript, FindPlugins);
			a_vm->RegisterFunction("GetLastError", kScript, GetLastError);
			a_vm->RegisterFunction("GetMasterPlugin", kScript, GetMasterPlugin);
			a_vm->RegisterFunction("GetLastEditingPlugin", kScript, GetLastEditingPlugin);
			a_vm->RegisterFunction("GetEditingPlugins", kScript, GetEditingPlugins);
			a_vm->RegisterFunction("GetLocalFormID", kScript, GetLocalFormID);
			a_vm->RegisterFunction("GetOwnerPlugin", kScript, GetOwnerPlugin);
			a_vm->RegisterFunction("GetReferences", kScript, GetReferences);
			a_vm->RegisterFunction("GetListContents", kScript, GetListContents);
			a_vm->RegisterFunction("GetVariable", kScript, GetVariable);
			a_vm->RegisterFunction("SetVariable", kScript, SetVariable);
			a_vm->RegisterFunction("UnsetVariable", kScript, UnsetVariable);
			a_vm->RegisterFunction("GetVersion", kScript, GetVersion);
			SKSE::log::info("Registered the WhichPlugin Papyrus functions");
			return true;
		}
	}

	void Register()
	{
		if (!SKSE::GetPapyrusInterface()->Register(Bind)) {
			SKSE::log::error("Couldn't register the WhichPlugin Papyrus functions");
		}
	}
}
