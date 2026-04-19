# Commands

List of commands that could be used by the player or in the server console.

- `.chars reload` - reloads module config, character cards and card additions; also queues a history and relationship reload from the database that runs after all currently pending events are processed (so no in-flight history is lost)
- `.chars condense [char_name]` - forcefully condenses current history, updates character definition and clears current history; also triggers relationship updates for party members that have enough mention data
- `.chars info [char_name]` - prints current character card with historical condensed additions and some basic statistics (number of additions, current number of messages in history)
- `.chars reset [char_name]` - removes all historical condensed additions, current chat history and relationship data for the character
- `.chars reset @ALL` - removes all historical condensed additions, current chat history and relationship data for all characters, basically restoring the module to its initial state
- `.chars history [char_name] [num=5]` - prints the last `num` entries from the character's in-memory chat history (capped at 20)
- `.chars relationship <char_name> <target_char_name>` - outputs `char_name`'s current LLM-generated relationship description towards `target_char_name`
- `.chars relationship_update <char_name> <target_char_name>` - forcefully queues an immediate relationship update LLM call for `char_name`'s relationship towards `target_char_name`
