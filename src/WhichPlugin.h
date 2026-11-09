#pragma once

// Console command: WhichPlugin (wp). "wp help" prints the full syntax in game.
//
//   wp                                   every loaded plugin with its load-order slot
//   wp patch                             guess: plugin name contains, or slot starts with (if hex)
//   wp "some" && "other" && !"patch"     && = AND, || = OR (AND binds first), ! = NOT
//   wp lobytes:FE                        every light plugin (slot prefix search)
//   wp 00012EB7                          which plugin owns the ID; the record's master, last edit, editor ID
//   wp edid~"^Iron" && type=WEAP         record search (formid/edid/type/keyword fields)
//   wp -d keyword:WeapTypeSword          options first: -c concise, -d detail rows, -l lo-agnostic, --int
//   ... > file | >> file | >>>           output to a file / append / clipboard;  | is a pipe (text tools)
namespace WhichPlugin
{
	// Registers the command with the console hook (full syntax) and in a console command slot
	// (so it shows up in the console's help and still works if the hook couldn't be installed).
	void Register();
}
