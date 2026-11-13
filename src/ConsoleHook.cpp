#include "ConsoleHook.h"

#include "ConsoleCommands.h"
#include "Pipeline.h"
#include "Redirect.h"
#include "Settings.h"
#include "TextTools.h"
#include "Variables.h"

namespace ConsoleHook
{
	namespace
	{
		struct Command
		{
			std::string longName;
			std::string shortName;
			Handler     handler;
		};

		std::vector<Command>& Commands()
		{
			static std::vector<Command> commands;
			return commands;
		}

		std::string_view Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t");
			if (first == std::string_view::npos) {
				return {};
			}
			return a_text.substr(first, a_text.find_last_not_of(" \t") - first + 1);
		}

		bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs)
		{
			return a_lhs.size() == a_rhs.size() &&
			       std::ranges::equal(a_lhs, a_rhs, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
		}

		// One of our commands and the text after its name, or nullptr if a_line isn't ours.
		const Command* Find(std::string_view a_line, std::string_view& a_args)
		{
			const auto start = a_line.find_first_not_of(" \t");
			if (start == std::string_view::npos) {
				return nullptr;
			}
			a_line.remove_prefix(start);
			const auto nameEnd = a_line.find_first_of(" \t");
			const auto name = a_line.substr(0, nameEnd);
			a_args = nameEnd == std::string_view::npos ? std::string_view{} : a_line.substr(nameEnd + 1);
			for (const auto& cmd : Commands()) {
				if (EqualsNoCase(name, cmd.longName) || EqualsNoCase(name, cmd.shortName)) {
					return &cmd;
				}
			}
			return nullptr;
		}

		// Runs the command line inside $(...): nothing is printed, and its output is returned.
		std::optional<std::vector<std::string>> RunSilently(std::string_view a_line, std::string& a_error)
		{
			std::string_view args;
			const auto*      cmd = Find(a_line, args);
			if (!cmd) {
				a_error = std::format("$({}) - only {} commands can run inside $(...)", a_line, Commands().empty() ? "our" : Commands().front().shortName);
				return std::nullopt;
			}
			std::string expanded;
			if (!Variables::Expand(args, &RunSilently, expanded, a_error)) {
				return std::nullopt;
			}
			Redirect::Target target;
			const auto       command = Redirect::Split(expanded, target);
			if (target.kind != Redirect::Kind::None || Pipeline::Split(command).size() > 1) {
				a_error = std::format("$({}) - no > or | inside $(...)", a_line);
				return std::nullopt;
			}
			ConsoleCommands::Capture capture{ false, true };
			cmd->handler(command);
			return capture.Lines();
		}

		// Returns true if the line was one of ours (and has been handled).
		bool TryHandle(std::string_view a_line)
		{
			std::string_view args;
			const auto*      cmd = Find(a_line, args);
			if (!cmd) {
				return false;
			}
			try {
				// Variables first, so $NAME can also stand in a file name after >.
				std::string expanded;
				std::string error;
				if (!Variables::Expand(args, &RunSilently, expanded, error)) {
					ConsoleCommands::Print(std::format("{}: {}", cmd->longName, error));
					return true;
				}

				Redirect::Target target;
				const auto       beforeRedirect = Redirect::Split(expanded, target);
				if (!target.error.empty()) {
					ConsoleCommands::Print(std::format("{}: {}", cmd->longName, target.error));
					return true;
				}
				const auto               stages = Pipeline::Split(beforeRedirect);
				const bool               redirecting = target.kind != Redirect::Kind::None;
				std::vector<std::string> lines;
				if (stages.size() == 1) {
					ConsoleCommands::Capture capture{ redirecting };
					cmd->handler(stages[0]);
					lines = capture.Lines();
				} else {
					// The command's whole output (past the console cap too) goes through the text
					// tools unprinted; only the result is shown.
					{
						ConsoleCommands::Capture capture{ true, true };
						cmd->handler(stages[0]);
						lines = capture.Lines();
					}
					for (std::size_t i = 1; i < stages.size(); ++i) {
						auto result = TextTools::Run(stages[i], lines, error);
						if (!result) {
							ConsoleCommands::Print(std::format("{}: | {}: {}", cmd->longName, Trim(stages[i]), error));
							return true;
						}
						lines = std::move(*result);
					}
					ConsoleCommands::Capture capture{ redirecting };
					const auto               cap = Settings::Get().maxConsoleLines;
					for (std::size_t i = 0; i < lines.size(); ++i) {
						if (i < cap) {
							ConsoleCommands::Print(lines[i]);
						} else if (redirecting) {
							ConsoleCommands::PrintRedirectOnly(lines[i]);
						} else {
							ConsoleCommands::Print(std::format("  ... {} more lines; add > name to save them all to a file", lines.size() - cap));
							break;
						}
					}
					lines = capture.Lines();
				}
				if (!Variables::IsCommand(stages[0])) {
					Variables::SetLastOutput(lines);
				}
				Redirect::Deliver(target, lines);
			} catch (const std::exception& e) {
				// An exception escaping into game code would terminate the game (e.g. std::regex
				// throws error_complexity/error_stack on pathological patterns).
				ConsoleCommands::Print(std::format("{}: stopped - {}", cmd->longName, e.what()));
				SKSE::log::error("{} '{}' threw: {}", cmd->longName, args, e.what());
			}
			return true;
		}

		struct CompileAndRunHook
		{
			static void thunk(RE::Script* a_script, RE::ScriptCompiler* a_compiler, RE::COMPILER_NAME a_name, RE::TESObjectREFR* a_targetRef)
			{
				if (a_script) {
					const auto line = a_script->GetCommand();
					if (TryHandle(line)) {
						return;
					}
				}
				func(a_script, a_compiler, a_name, a_targetRef);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		HMODULE ModuleOf(std::uintptr_t a_addr)
		{
			HMODULE module = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(a_addr), &module);
			return module;
		}
	}

	void AddCommand(std::string_view a_longName, std::string_view a_shortName, Handler a_handler)
	{
		Commands().push_back({ std::string{ a_longName }, std::string{ a_shortName }, a_handler });
	}

	namespace
	{
		std::uintptr_t CompileAndRunAddress()
		{
			// Script::CompileAndRun was renumbered in 1.6.1130 (same rule CommonLib uses in Script.cpp).
			const auto ae = REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_6_1130 ? 441582 : 21890;
			return REL::Relocation<std::uintptr_t>{ REL::RelocationID(21416, ae) }.address();
		}

		// Verifies that a_address is a call to Script::CompileAndRun (or to another mod's hook of
		// it), then redirects it to Hook::thunk; Hook::func keeps where it used to go.
		template <class Hook>
		bool HookCallSite(std::string_view a_what, std::uintptr_t a_address)
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_address);
			if (bytes[0] != 0xE8) {
				SKSE::log::error("{} hook not installed: expected a call instruction at {:X}, found byte {:02X}", a_what, a_address, bytes[0]);
				return false;
			}
			std::int32_t displacement;
			std::memcpy(&displacement, bytes + 1, sizeof(displacement));
			const auto target = a_address + 5 + static_cast<std::intptr_t>(displacement);

			if (target == CompileAndRunAddress()) {
				SKSE::log::info("{} hook site verified (calls Script::CompileAndRun)", a_what);
			} else if (ModuleOf(target) != GetModuleHandleW(nullptr)) {
				// Another mod (e.g. Console Command Extender) already hooked this call; we chain in front of it.
				SKSE::log::info("{} hook site is already hooked by another mod (target {:X}); chaining", a_what, target);
			} else {
				SKSE::log::error("{} hook not installed: the call at {:X} goes to {:X}, not Script::CompileAndRun ({:X})",
					a_what, a_address, target, CompileAndRunAddress());
				return false;
			}
			Hook::func = SKSE::GetTrampoline().write_call<5>(a_address, Hook::thunk);
			SKSE::log::info("{} hook installed", a_what);
			return true;
		}

		// Same interception for the other two direct callers of CompileAndRun in 1.6.1170 (442673 runs
		// the console's queue of delayed commands; 442679 is similar). Harmless if never used for our lines.
		template <int N>
		struct ExtraHook
		{
			static void thunk(RE::Script* a_script, RE::ScriptCompiler* a_compiler, RE::COMPILER_NAME a_name, RE::TESObjectREFR* a_targetRef)
			{
				if (a_script && TryHandle(a_script->GetCommand())) {
					return;
				}
				func(a_script, a_compiler, a_name, a_targetRef);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	namespace
	{
		// "bat" batch files don't go through CompileAndRun at all: the batch runner (21891) has its own
		// inlined compile-and-run loop. For each line it first calls ConsoleLogPrinter::Print (the "> line"
		// echo), then compiles the same 260-byte buffer. We hook that Print: after the echo, if the line is
		// ours we run it and turn the buffer into a comment so the game compiles nothing.
		// ConsoleLogPrinter's vtable (215004) is only referenced by the batch runner (checked on 1.6.1170).
		struct BatchLineHook
		{
			static void thunk(void* a_printer, char* a_line)
			{
				func(a_printer, a_line);
				if (a_line && TryHandle(a_line)) {
					constexpr std::size_t kBatchLineBuffer = 0x104;  // the runner's line buffer
					strcpy_s(a_line, kBatchLineBuffer, "; handled by WhichPlugin");
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		bool HookBatchLines()
		{
			REL::Relocation<std::uintptr_t> vtable{ REL::ID(215004) };
			const auto                       slot = *reinterpret_cast<const std::uintptr_t*>(vtable.address() + sizeof(void*));
			const auto                       expected = REL::Relocation<std::uintptr_t>{ REL::ID(51105) }.address();
			if (slot != expected && ModuleOf(slot) == GetModuleHandleW(nullptr)) {
				SKSE::log::error("Batch-file hook not installed: ConsoleLogPrinter::Print is at {:X}, expected {:X}", slot, expected);
				return false;
			}
			BatchLineHook::func = vtable.write_vfunc(1, BatchLineHook::thunk);
			SKSE::log::info("Batch-file hook installed{}", slot == expected ? " (verified ConsoleLogPrinter::Print)" : " (chained after another mod's hook)");
			return true;
		}
	}

	bool Install()
	{
		// The console's call to Script::CompileAndRun (SE 1.5.97 / AE 1.6.1170+).
		const REL::Relocation<std::uintptr_t> site{ REL::RelocationID(52065, 52952), REL::Relocate(0xE2, 0x52) };
		const bool                            installed = HookCallSite<CompileAndRunHook>("Console", site.address());

		// The other two callers only have IDs from 1.6.1130 on; older runtimes keep console-only support.
		if (REL::Module::IsAE() && REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_6_1130) {
			HookCallSite<ExtraHook<1>>("Queued console commands (442673)", REL::Relocation<std::uintptr_t>{ REL::ID(442673), 0xC5 }.address());
			HookCallSite<ExtraHook<2>>("Console commands (442679)", REL::Relocation<std::uintptr_t>{ REL::ID(442679), 0x163 }.address());
			HookBatchLines();
		}
		return installed;
	}
}
