# Commands

List of commands that can be used by the player or in the server console.

- `.chars reload` — reloads module config, prompts, character cards, card additions and character data; also queues a history and relationship reload from the database that runs after all currently pending events are processed (so no in-flight history is lost)
- `.chars condense [char_name]` — forcefully condenses current history, updates character definition and clears current history; also triggers relationship updates for party members that have enough mention data
- `.chars info [char_name]` — prints current character card with historical condensed additions and some basic statistics (number of additions, current number of messages in history, roll chance modifier)
- `.chars reset [char_name]` — removes all historical condensed additions, current chat history and relationship data for the `char_name` character
- `.chars reset @ALL` — removes all historical condensed additions, current chat history and relationship data for all characters, basically restoring the module to its initial state
- `.chars history [char_name] [num=5]` — prints the last `num` entries from the character's in-memory chat history (capped at 20)
- `.chars relationship [char_name] [target_char_name]` — outputs `char_name`'s current LLM-generated relationship description towards `target_char_name`
- `.chars relationship_update [char_name] [target_char_name]` — forcefully queues an immediate relationship update LLM call for `char_name`'s relationship towards `target_char_name`
- `.chars roll_modifier <char_name> [roll_modifier]` — sets or displays the per-character roll chance modifier (integer from -100 to 100). A positive value makes the character more talkative on average (adds to every roll chance), a negative value makes them less talkative (duh). Does not affect whisper or mention reply chances. Omit `[roll_modifier]` to display the current value.
- `.chars context [char_name]` — builds and prints the current `{context}` variable for the character (defined by `PBC.CharacterContext` with all template variables substituted). Mostly useful for debugging to inspect what context the character would see at the current moment.
- `.chars web` — generates a one-time password for the web interface and displays the connection URL. In-game only (not available from console).
- `.chars apitest [query=hi]` — sends a quick test request to the configured LLM API with the system prompt "Answer in one single short sentence." and prints the response (or an error message if the request fails)
