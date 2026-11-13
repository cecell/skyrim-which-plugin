#pragma once

// Bash-style variables for our console commands:
//   wp export NAME=value     set NAME (the rest of the line; surrounding quotes are removed)
//   wp unset NAME            remove it
//   wp env                   list the variables, built-ins with their current values
//   $NAME  ${NAME}           replaced by the value anywhere outside '...' quotes (\$ is a plain $)
//   $(wp ...)                replaced by that command's output, lines joined by spaces
// Variables you set are kept in My Games\Skyrim Special Edition\wp\vars.json. The built-ins can't be
// set: SELECTED, CROSSHAIR, PLAYER, CELL, WORLDSPACE (form IDs), LAST_RECORD (the last record shown
// in detail), LAST_FILE, LAST_OUTPUT (the last command's output) and CLIPBOARD.
namespace Variables
{
	// Runs one of our command lines (from $(...)) without printing it, and returns its output.
	// Returns nullopt with a_error if the line isn't one of our commands.
	using Runner = std::function<std::optional<std::vector<std::string>>(std::string_view a_line, std::string& a_error)>;

	// Replaces $NAME, ${NAME} and $(...) in a_text. False (with a_error) if a variable isn't set.
	bool Expand(std::string_view a_text, const Runner& a_run, std::string& a_out, std::string& a_error);

	// wp export / unset / env. Returns false if a_args isn't one of them.
	bool RunCommand(std::string_view a_args);
	bool IsCommand(std::string_view a_args);

	void SetLastOutput(const std::vector<std::string>& a_lines);
	void SetLastRecord(RE::FormID a_id);
}
