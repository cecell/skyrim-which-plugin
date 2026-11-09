#pragma once

// Splits a command line into pipeline stages on a single '|' outside quotes ('||' is the OR
// operator and is left alone). Stage 0 is the command itself; later stages will be text tools
// (grep, sed, sort, ...) that each receive the previous stage's output lines.
namespace Pipeline
{
	std::vector<std::string_view> Split(std::string_view a_text);
}
