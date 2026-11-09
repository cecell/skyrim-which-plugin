#pragma once

// Adds console commands by taking over unused vanilla console command slots (the technique
// Console Command Extender uses). Slots are only taken if they still hold their original vanilla
// command, so we never overwrite a slot another mod already claimed.
namespace ConsoleCommands
{
	struct Definition
	{
		const char*                          longName;
		const char*                          shortName;
		const char*                          help;
		RE::SCRIPT_PARAMETER*                params;
		std::uint16_t                        numParams;
		RE::SCRIPT_FUNCTION::Execute_t*      execute;
	};

	bool Register(const Definition& a_def);

	// Prints one line to the console (safe for text containing '%'). While a Capture is active the
	// line is also recorded, so the output can be redirected to a file or the clipboard.
	void Print(std::string_view a_line);

	// Records a line for the redirect target only (not shown in the console). Used for results
	// beyond the console cap when the output is going to a file or the clipboard.
	void PrintRedirectOnly(std::string_view a_line);

	// True while the current command's output is being redirected (> >> >>>).
	bool IsRedirecting();

	class Capture
	{
	public:
		explicit Capture(bool a_redirecting);
		~Capture();
		Capture(const Capture&) = delete;
		Capture& operator=(const Capture&) = delete;

		[[nodiscard]] const std::vector<std::string>& Lines() const { return _lines; }

	private:
		std::vector<std::string>  _lines;
		std::vector<std::string>* _previous;
		bool                      _previousRedirecting;
	};

	// The string argument of a command, or nullopt when the user gave none.
	std::optional<std::string> GetStringArg(RE::SCRIPT_FUNCTION::ScriptData* a_scriptData);
}
