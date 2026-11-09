#pragma once

// Output redirection for our console commands, split off the end of the typed line:
//   ... > name       write the output to a file (replaces it)
//   ... >> name      append the output to a file
//   ... >>           append to the last file used this session
//   ... >>>          copy the output to the Windows clipboard (anything after it is ignored)
// File names are relative to Documents\My Games\Skyrim Special Edition; ".txt" is added when
// there is no extension. A '>' inside quotes is part of the search, not a redirect.
namespace Redirect
{
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
		std::filesystem::path path;   // absolute, for Replace/Append
		std::string           error;  // non-empty = the redirect itself is invalid
	};

	// Splits a_args into the command's own arguments (returned) and the redirect (a_target).
	std::string_view Split(std::string_view a_args, Target& a_target);

	// Delivers captured output and prints a one-line confirmation (or the error) to the console.
	void Deliver(const Target& a_target, const std::vector<std::string>& a_lines);
}
