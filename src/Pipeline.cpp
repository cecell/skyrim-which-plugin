#include "Pipeline.h"

namespace Pipeline
{
	std::vector<std::string_view> Split(std::string_view a_text)
	{
		std::vector<std::string_view> stages;
		std::size_t                   start = 0;
		char                          quote = 0;
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			const char c = a_text[i];
			if (quote) {
				if (quote == '\'' && c == '\\' && i + 1 < a_text.size() && a_text[i + 1] == '\'') {
					++i;
				} else if (c == quote) {
					quote = 0;
				}
			} else if (c == '"' || c == '\'') {
				quote = c;
			} else if (c == '|') {
				if (i + 1 < a_text.size() && a_text[i + 1] == '|') {
					++i;  // '||' is OR, not a pipe
					continue;
				}
				stages.push_back(a_text.substr(start, i - start));
				start = i + 1;
			}
		}
		stages.push_back(a_text.substr(start));
		return stages;
	}
}
