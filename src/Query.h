#pragma once

// Parses the text after "wp" into search terms.
//
//   group ('||' group)*        any group may match (OR)
//   group := term ('&&' term)* all terms in a group must match (AND binds tighter than OR)
//   term  := ['!'] [field op] value         '!' negates the term (NOT)
// (A single '|' is a pipe and never reaches this parser; see Pipeline.h.)
//   op    := ':'  contains  (lobytes: starts with; formid: this exact ID)
//          | '='  exact
//          | '~'  regular expression
//   value := "double quoted"  (taken literally; plugin names can't contain ")
//          | 'single quoted'  (\' is a literal apostrophe)
//          | bare text        (up to the next && or ||, trimmed)
//   field := plugin-name | plugin-master | lobytes | formid | edid | type | keyword
//            (none = guess: plugin name or slot)
//            regex: is shorthand for plugin-name~
namespace Query
{
	enum class Field
	{
		Any,
		PluginName,
		PluginMaster,
		LoBytes,
		FormId,
		EditorId,
		Type,     // record type signature: WEAP, NPC_, ...
		Keyword,  // has a keyword, given by form ID or editor ID (~: any keyword whose editor ID matches)
	};

	enum class Match
	{
		Contains,
		Exact,
		Regex,
	};

	struct Term
	{
		Field       field{ Field::Any };
		Match       match{ Match::Contains };
		bool        negated{ false };
		std::string value;
		std::string source;  // the term as typed, for messages
	};

	struct Parsed
	{
		std::vector<std::vector<Term>> groups;  // OR of ANDs
		std::vector<Term>              terms;   // every term, flattened
		std::vector<std::string>       errors;
		std::vector<std::string>       warnings;
	};

	Parsed Parse(std::string_view a_text);

	std::string_view FieldName(Field a_field);
	bool             IsRecordField(Field a_field);

	// Hex helpers shared with the command.
	bool                         IsHex(std::string_view a_text);
	std::string                  NonHexChars(std::string_view a_text);
	std::optional<std::uint32_t> ParseHex(std::string_view a_text);
	std::string                  StripHexPrefix(std::string_view a_text);
}
