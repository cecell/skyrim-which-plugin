#pragma once

// Output redirection for our console commands, split off the end of the typed line:
//   ... > name       write the output to a file (replaces it, unless bOverwriteNeedsConfirm)
//   ... >! name      write the output to a file, replacing it in any case
//   ... >> name      append the output to a file
//   ... >>           append to the last file used this session
//   ... >>>          copy the output to the Windows clipboard (anything after it is ignored)
// File names are relative to Documents\My Games\Skyrim Special Edition\wp (full paths and '..' only
// with bWriteFileAnywhere); ".txt" is added when there is no extension, and only text file
// extensions are allowed. A '>' inside quotes is part of the search, not a redirect.
namespace Redirect
{
	// Documents\My Games\Skyrim Special Edition\wp, or nullopt if the game's folder isn't known.
	std::optional<std::filesystem::path> OutputFolder();

	// The last file written or appended to this session (empty if none).
	const std::filesystem::path& LastFile();

	// True for the text file types output may be written to: .txt .csv .tsv .log .md .ini .toml
	// .xml .yaml .yml and .json*.
	bool AllowedExtension(const std::filesystem::path& a_path);

	enum class Kind
	{
		None,
		Replace,
		Append,
		Clipboard,
	};

	struct Target
	{
		Kind                  kind{ Kind::None };
		bool                  force{ false };  // '>!': replace even with bOverwriteNeedsConfirm
		std::filesystem::path path;            // absolute, for Replace/Append
		std::string           error;  // non-empty = the redirect itself is invalid
	};

	// Splits a_args into the command's own arguments (returned) and the redirect (a_target).
	std::string_view Split(std::string_view a_args, Target& a_target);

	// Delivers captured output and prints a one-line confirmation (or the error) to the console.
	void Deliver(const Target& a_target, const std::vector<std::string>& a_lines);
}
