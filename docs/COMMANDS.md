# Commands

List of commands that can be used by the player or in the server console.

| Command | Description |
|---|---|
| `.chars reload` | Reloads module config, prompts, character cards, and data. Also reloads history and relationships from the database after all pending events are processed. |
| `.chars condense <name>` | Condenses a character's history into their card definition and clears the history. Also triggers relationship updates for party members. |
| `.chars info <name>` | Prints the character's card, condensed additions, and basic stats (addition count, history length, roll modifier). |
| `.chars reset <name>` | Removes all additions, history, and relationship data for a character. |
| `.chars reset @ALL` | Removes all additions, history, and relationship data for every character. |
| `.chars history <name> [num=5]` | Shows the last `num` entries from a character's chat history (max 20). |
| `.chars relationship <name> <target>` | Shows a character's LLM-generated relationship description towards another character. |
| `.chars relationship-update <name> <target>` | Queues an immediate relationship update LLM call for a character towards a target. |
| `.chars roll-modifier <name> [value]` | Sets or displays the per-character roll chance modifier (−100 to 100). Positive = more talkative, negative = less talkative. Does not affect whisper or mention chances. Omit value to display current. |
| `.chars context <name>` | Prints the character's current context variable with all template substitutions applied (useful for debugging). |
| `.chars web` | Generates a one-time password for the web interface and displays the connection URL. In-game only. |
| `.chars api-test [query=hi]` | Sends a test request to the main LLM API and prints the response. |
| `.chars alt-api-test [query=hi]` | Same as `api-test` but uses the alternative model configuration. |
| `.chars narrate <name> <message>` | Adds a narrator line to a character's chat history. Does not trigger a response. In-game only. |
| `.chars narrate-party <message>` | Adds a narrator line to every group character's chat history. Does not trigger responses. In-game only. |
| `.chars trigger <name>` | Forces a character to respond (party message if grouped, say otherwise). The trigger event is not written into history. |
