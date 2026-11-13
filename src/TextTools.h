#pragma once

// The text tools that can follow a pipe ('|'), each working on the previous stage's output lines,
// with the options people know from Linux:
//   grep [-i -v -c -n -F] PATTERN     lines matching a regex (-F: plain text)
//   sed [-n] 'SCRIPT'                 s/re/new/[g i p], d, p; addresses N, $, /re/, A,B; ';' between
//   sort [-r -n -u -f]                sort (-n numbers, -u drop duplicates, -f ignore case)
//   uniq [-c -d -i]                   collapse repeated lines (-c count, -d only repeated ones)
//   head [-n N | -N]  tail [-n N | -N]   first or last N lines (10)
//   wc [-l -w -c]                     count lines, words, characters
//   echo TEXT                         replace the lines with TEXT
//   cat [-n]                          pass the lines on (-n numbers them)
// Regexes are ECMAScript (as in JavaScript). Nothing here touches the game.
namespace TextTools
{
	// Runs one stage, e.g. "grep -i sword". nullopt with a_error on a usage error.
	std::optional<std::vector<std::string>> Run(std::string_view a_stage, const std::vector<std::string>& a_lines, std::string& a_error);

	// Splits a stage into words the way a shell does: spaces separate, "..." and '...' group
	// (in '...', \' is an apostrophe). False with a_error on a missing closing quote.
	bool Words(std::string_view a_text, std::vector<std::string>& a_out, std::string& a_error);
}
