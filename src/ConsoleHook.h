#pragma once

// Intercepts console lines before the game's script compiler parses them, so our commands can use
// any syntax (quotes, pipes, colons, backslash escapes, any number of arguments). This is the same
// call site Console Command Extender hooks for its "> file" redirect; hooks there chain safely.
namespace ConsoleHook
{
	// Receives everything typed after the command name, untouched.
	using Handler = void (*)(std::string_view a_args);

	// Must be called before Install(). Names are matched case-insensitively.
	void AddCommand(std::string_view a_longName, std::string_view a_shortName, Handler a_handler);

	// Returns false (and logs why) if the hook site doesn't look as expected; the game is left untouched.
	// Call at kPostPostLoad: Console Command Extender hooks the same call at kPostLoad, and the last
	// hook installed runs first - we must run first so its own "> file" parser never sees our lines.
	bool Install();
}
