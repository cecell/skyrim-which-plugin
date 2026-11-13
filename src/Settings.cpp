#include "Settings.h"

namespace Settings
{
	namespace
	{
		Values& Mutable()
		{
			static Values values;
			return values;
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		class Ini
		{
		public:
			explicit Ini(std::filesystem::path a_path) :
				_path(std::move(a_path)) {}

			// The value with any trailing "; comment" and surrounding spaces removed; nullopt if absent.
			std::optional<std::string> Read(const char* a_section, const char* a_key) const
			{
				char buffer[512]{};
				constexpr auto kMissing = "\x01";
				GetPrivateProfileStringA(a_section, a_key, kMissing, buffer, sizeof(buffer), _path.string().c_str());
				std::string value{ buffer };
				if (value == kMissing) {
					return std::nullopt;
				}
				if (const auto semicolon = value.find(';'); semicolon != std::string::npos) {
					value.erase(semicolon);
				}
				const auto first = value.find_first_not_of(" \t");
				const auto last = value.find_last_not_of(" \t");
				return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
			}

			void Bool(const char* a_section, const char* a_key, bool& a_out, std::vector<std::string>& a_problems) const
			{
				if (const auto v = Read(a_section, a_key)) {
					const auto lower = Lower(*v);
					if (lower == "1" || lower == "true") {
						a_out = true;
					} else if (lower == "0" || lower == "false") {
						a_out = false;
					} else {
						a_problems.push_back(std::format("[{}] {} = {}: use 1 or 0; keeping {}", a_section, a_key, *v, a_out ? 1 : 0));
					}
				}
			}

			void Count(const char* a_section, const char* a_key, std::size_t a_min, std::size_t a_max, std::size_t& a_out, std::vector<std::string>& a_problems) const
			{
				if (const auto v = Read(a_section, a_key)) {
					std::size_t parsed = 0;
					const auto [end, ec] = std::from_chars(v->data(), v->data() + v->size(), parsed);
					if (ec != std::errc{} || end != v->data() + v->size() || parsed < a_min || parsed > a_max) {
						a_problems.push_back(std::format("[{}] {} = {}: use a whole number from {} to {}; keeping {}", a_section, a_key, *v, a_min, a_max, a_out));
					} else {
						a_out = parsed;
					}
				}
			}

		private:
			std::filesystem::path _path;
		};
	}

	const Values& Get()
	{
		return Mutable();
	}

	void Load(std::vector<std::string>& a_problems)
	{
		auto&     v = Mutable();
		const Ini ini{ std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "WhichPlugin.ini" };

		if (const auto name = ini.Read("General", "sShortCommand")) {
			const bool ok = !name->empty() && name->size() <= 16 && std::ranges::all_of(*name, [](unsigned char c) { return std::isalnum(c) != 0; });
			if (ok) {
				v.shortCommand = *name;
			} else {
				a_problems.push_back(std::format("[General] sShortCommand = {}: use 1 to 16 letters and digits; keeping {}", *name, v.shortCommand));
			}
		}
		if (const auto level = ini.Read("General", "sLogLevel")) {
			const auto lower = Lower(*level);
			if (lower == "trace") {
				v.logLevel = spdlog::level::trace;
			} else if (lower == "debug") {
				v.logLevel = spdlog::level::debug;
			} else if (lower == "info") {
				v.logLevel = spdlog::level::info;
			} else if (lower == "warn") {
				v.logLevel = spdlog::level::warn;
			} else if (lower == "error") {
				v.logLevel = spdlog::level::err;
			} else {
				a_problems.push_back(std::format("[General] sLogLevel = {}: use trace, debug, info, warn or error; keeping info", *level));
			}
		}
		ini.Count("General", "iMaxConsoleLines", 1, 100000, v.maxConsoleLines, a_problems);
		if (const auto layout = ini.Read("General", "sDefaultLayout")) {
			const auto lower = Lower(*layout);
			if (lower == "normal") {
				v.defaultLayout = Layout::Default;
			} else if (lower == "concise") {
				v.defaultLayout = Layout::Concise;
			} else if (lower == "detail") {
				v.defaultLayout = Layout::Detail;
			} else {
				a_problems.push_back(std::format("[General] sDefaultLayout = {}: use normal, concise or detail; keeping normal", *layout));
			}
		}

		ini.Bool("Output", "bWriteFileAnywhere", v.writeFileAnywhere, a_problems);
		ini.Bool("Output", "bOverwriteNeedsConfirm", v.overwriteNeedsConfirm, a_problems);
		ini.Bool("Open", "bAllowOpenFolders", v.allowOpenFolders, a_problems);
		ini.Bool("Open", "bAllowOpenURL", v.allowOpenURL, a_problems);

		ini.Count("Variables", "iMaxVariables", 0, 100000, v.maxVariables, a_problems);
		ini.Count("Variables", "iMaxVariableLength", 1, 10000000, v.maxVariableLength, a_problems);
		ini.Bool("Variables", "bAllowClipboardRead", v.allowClipboardRead, a_problems);
	}
}
