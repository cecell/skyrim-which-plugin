#pragma once

// Data\SKSE\Plugins\WhichPlugin.ini, read once when the game starts. A missing file or key keeps the
// default below; a value that can't be used keeps it too, and the log says why.
namespace Settings
{
	enum class Layout
	{
		Default,
		Concise,
		Detail,
	};

	struct Values
	{
		// [General]
		std::string               shortCommand{ "wp" };  // sShortCommand: the short console name
		spdlog::level::level_enum logLevel{ spdlog::level::info };
		std::size_t               maxConsoleLines{ 100 };  // iMaxConsoleLines: results shown; a redirect gets all
		Layout                    defaultLayout{ Layout::Default };

		// [Output]
		bool writeFileAnywhere{ false };      // bWriteFileAnywhere: allow full paths and ..
		bool overwriteNeedsConfirm{ false };  // bOverwriteNeedsConfirm: '>' refuses an existing file; '>!' replaces it

		// [Open]
		bool allowOpenFolders{ true };  // bAllowOpenFolders: wp open, wp open last
		bool allowOpenURL{ true };      // bAllowOpenURL: wp help --web, wp uesp, wp json
	};

	const Values& Get();

	// Reads the ini. Call before SKSE::Init (for the log level); a_problems collects what to log
	// once logging has started.
	void Load(std::vector<std::string>& a_problems);
}
