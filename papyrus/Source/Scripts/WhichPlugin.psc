ScriptName WhichPlugin Hidden
{Which Plugin's searches for other mods' scripts. Needs WhichPlugin.dll (SKSE).
A query is written as you would after "wp" in the console, without options, > or |.}

; The records a search finds, sorted by form ID, at most aiMax (1 to 10000). Empty if nothing
; matches or the query is wrong: GetLastError() says why.
; e.g. Form[] swords = WhichPlugin.Find("type=WEAP && name:sword")
Form[] Function Find(string asQuery, int aiMax = 1000) global native

; The names of the loaded plugins a plugin search matches ("" = all of them), in load order.
string[] Function FindPlugins(string asQuery) global native

; Why the last call of this script returned nothing ("" if it worked).
string Function GetLastError() global native

; The plugin that defines akForm, the last one to edit it, and every plugin with it, in load order.
; "" or empty for forms created at runtime.
string Function GetMasterPlugin(Form akForm) global native
string Function GetLastEditingPlugin(Form akForm) global native
string[] Function GetEditingPlugins(Form akForm) global native

; akForm's ID without its load-order byte(s), and the plugin its ID belongs to, so that
; Game.GetFormFromFile(GetLocalFormID(f), GetOwnerPlugin(f)) gives f back. -1 and "" for forms
; created at runtime.
int Function GetLocalFormID(Form akForm) global native
string Function GetOwnerPlugin(Form akForm) global native

; The references placed from akBase that are in memory (the loaded cells and persistent
; references), by form ID.
ObjectReference[] Function GetReferences(Form akBase) global native

; What a leveled list or form list holds, each entry once. abNested: the entries of nested lists too.
Form[] Function GetListContents(Form akList, bool abNested = true) global native

; wp's variables (wp export, $NAME). GetVariable also reads the built-in ones (SELECTED, PLAYER,
; CELL, ...). Changes are saved like wp export's. On failure: "" or false, and GetLastError().
string Function GetVariable(string asName) global native
bool Function SetVariable(string asName, string asValue) global native
bool Function UnsetVariable(string asName) global native

; The DLL's version, e.g. "0.91".
string Function GetVersion() global native
