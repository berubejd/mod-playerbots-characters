# Playerbots Characters (PBC)

This is an [AzerothCore](https://www.azerothcore.org) module built around [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots), breathing new life into bots by turning them into true in-game characters — companions with personality, memory, and relationships. Heavily inspired by [mod-ollama-chat](https://github.com/DustinHendrickson/mod-ollama-chat), but taking a different, more complex approach — focusing on the roleplaying experience rather than emulating real WoW players.


## How It Works

This module extends the bots provided by `mod-playerbots` into full characters — giving them a voice, memories, relationships, and the ability to react to events and converse with companions. The bot logic (combat, movement, questing) remains entirely untouched; this module only adds the personality layer on top.

The module hooks into various in-game events (chat messages, item pickups, duels, level-ups, location changes, boss kills, quest taken, quest completed) and dispatches them to the characters in the player's party based on configurable reply chances. When a character "rolls" to respond, the module builds a prompt from its character card, accumulated chat history, current relationships with other party members, live context (location, time of day, nearby characters, combat status), and the event itself. This prompt is then sent to an OpenAI-compatible LLM API, and the model's response is spoken by the character in-game.

For regular (non-whisper, non-mention) chat messages, characters roll in a random order. The first character rolls at `PBC.ReplyChanceMessage`. Each time a character successfully rolls to answer, the chance for the next character is reduced by `PBC.RollPenaltyOnAnswer`. If a character fails its roll, the next character rolls at the same chance (no penalty). This guarantees that with a high initial chance someone will respond, while preventing too many characters from answering at once. When a player mentions specific characters by name, only those characters roll (at `PBC.ReplyChanceMention`), independently of the penalty system.

Over time, chat history grows. When it reaches the configured token limit (`PBC.MaxCtx`), a condensation process kicks in — the LLM is asked to summarize the history, and the result is appended to the character's card as a permanent addition. The in-memory history is then trimmed, keeping only the most recent lines. This way, characters gradually develop memories and personality traits without the context growing unbounded.

Relationships are tracked for each individual character in relation to other characters and real players. Every time a name is mentioned enough times in a character's history (controlled by `PBC.RelationshipUpdateThreshold`), a relationship update LLM call is triggered, generating or updating a brief description of how the character feels about that person. These relationship descriptions are included in future prompts, giving characters a sense of continuity with their companions.

Note that this module only handles the character layer — it does not influence any of the bot logic that `mod-playerbots` uses (combat, movement, questing and so on).


## Installation

Since mod-playerbots is an obvious hard requirement, follow their [Installation Guide](https://github.com/mod-playerbots/mod-playerbots/wiki/Installation-Guide) until you have a working acore installation with playerbots enabled.

Next, clone this repository into the `modules` directory of your acore sources and rebuild the server normally.

> [!NOTE]
> 1. Only Linux is officially supported as a build target. Technically nothing should stop you from using the module on Windows, but this is untested and unsupported.
> 2. The module includes a bundled copy of [nlohmann/json](https://github.com/nlohmann/json) in `deps/nlohmann/json.hpp`, so no external JSON library is required. The build system will use the bundled version by default, falling back to a system-installed version if available.

If [mod_weather_vibe](https://github.com/hermensbas/mod_weather_vibe) is also installed, weather states from it will be used to define the character's scene.


## Configuration

Copy `env/dist/etc/modules/playerbots_characters.conf.dist` as `env/dist/etc/modules/playerbots_characters.conf` and adjust it as needed.

> [!IMPORTANT]
> When updating the module, always compare your `.conf` with the new `.conf.dist` to ensure any newly added parameters are present in your config. The default values, especially prompts, may also get changed in newer versions.

### Model Connection Setup

The module supports two API formats, controlled by `PBC.APIType`:

- **`openai`** (default) — OpenAI-compatible `/chat/completions` endpoint. The module appends `/chat/completions` to `PBC.BaseUrl` and sends the API key via `Authorization: Bearer` header.
- **`anthropic`** — Anthropic Messages API `/messages` endpoint. The module appends `/messages` to `PBC.BaseUrl` and sends the API key via `x-api-key` header with the `anthropic-version: 2023-06-01` header.

You need to configure at least `PBC.BaseUrl`, `PBC.Model` and `PBC.ApiKey` before the module can generate responses. The relevant config options are in the **API CONNECTION** and **MODEL PARAMETERS** sections of the config file. After configuring, you can use `.chars apitest` to quickly verify that the connection is working.

Due to the complexity and length of the prompts, locally-run models on average home hardware will generally struggle and produce low-quality output as context grows. A cloud-based model with a large context window is recommended. Make sure to also adjust `PBC.MaxCtx` accordingly — a good starting point is around 25% of the model's total context window. Aim for at least 32k in general, anything less could lead to poor efficiency of character relationship tracking and card additions.

Choosing the right model can be tricky. Two tested configurations are listed below.

#### DeepSeek

| Setting | Value |
|---|---|
| `PBC.APIType` | `openai` |
| `PBC.BaseUrl` | `https://api.deepseek.com/v1` |
| `PBC.Model` | `deepseek-chat` |
| `PBC.Temperature` | `1.6` |
| `PBC.MaxCtx` | `32768` |
| `PBC.ModelExtraParameters` | `'frequency_penalty':0.5,'presence_penalty':0.2` |
| `PBC.ApiKey` | your API key from [DeepSeek platform](https://platform.deepseek.com/) |

DeepSeek offers a reasonable cost/capabilities compromise and can be considered the cheapest viable option. The `frequency_penalty` and `presence_penalty` extra parameters help reduce repetitive output. Expect to spend under $0.5 for several hours of play.

#### GLM (Zhipu AI)

| Setting | Value |
|---|---|
| `PBC.APIType` | `openai` |
| `PBC.BaseUrl` | `https://api.z.ai/api/paas/v4` |
| `PBC.Model` | `glm-5.1` |
| `PBC.Temperature` | `1.0` |
| `PBC.MaxCtx` | `32768` |
| `PBC.ModelExtraParameters` | `'thinking':{'type':'disabled'}` |
| `PBC.ApiKey` | your API key from [Z.ai](https://z.ai/manage-apikey/apikey-list) |

GLM 5.1 has a built-in "thinking" mode that is incompatible with the module's prompt structure — disabling it via `ModelExtraParameters` is required for correct output. The model handles the required tasks impressively well, though the cost adds up fairly quickly. Expect to spend around $2 per long game session with a full party.

#### Other Models

Any OpenAI-compatible API should work with `PBC.APIType = openai` — just set `PBC.BaseUrl` to the endpoint (the module appends `/chat/completions` automatically), `PBC.Model` to the model identifier, and `PBC.ApiKey` to your bearer token. If the endpoint doesn't require authentication (e.g. a local Ollama or LM Studio instance), leave `PBC.ApiKey` empty.

Use `PBC.ModelExtraParameters` to inject provider-specific JSON into the request body — this works the same way for both `openai` and `anthropic` API types. Make sure to use parameter names that are valid for your chosen API type (e.g. `top_p` and `top_k` for Anthropic, `frequency_penalty` and `presence_penalty` for OpenAI-compatible providers). Single quotes are used instead of double quotes and are automatically replaced at runtime:

```
PBC.ModelExtraParameters = 'frequency_penalty':0.5,'presence_penalty':0.2
```

becomes `"frequency_penalty":0.5,"presence_penalty":0.2` in the request.

When switching providers, pay attention to `PBC.Temperature` — acceptable ranges vary between models. Check the provider's documentation for the recommended value.

### Playerbots Adjustments

Recommended adjustments to the playerbots config (`playerbots.conf`), to make bots less talkative in order to not interfere with the new character logic:

| Setting | Default | Recommended | Purpose |
|---|---|---|---|
| `AiPlayerbot.EnableBroadcasts` | 1 | 0 | Disables loot / quest / kill broadcasts |
| `AiPlayerbot.RandomBotTalk` | 1 | 0 | Disables random talking in say / yell / general channels |
| `AiPlayerbot.RandomBotEmote` | 0 | 0 | Disables random emoting |
| `AiPlayerbot.RandomBotSuggestDungeons` | 1 | 0 | Disables dungeon suggestions in chat |
| `AiPlayerbot.EnableGreet` | 0 | 0 | Disables greeting when invited |
| `AiPlayerbot.GuildFeedback` | 1 | 0 | Disables guild event chatting |
| `AiPlayerbot.RandomBotSayWithoutMaster` | 0 | 0 | Disables bots talking without a master |
| `AiPlayerbot.RPWarningCooldown` | 300 | 999999 | Increases delay between "missing reagents" messages and such |


## Usage

Start the server, set up some altbots for yourself or invite existing random bots, then write cards for them as you see fit. See `characters/Example.card.txt` for an example of how to write a character card. The character name in the filename must match the in-game character name exactly to be picked up. It is recommended to write cards in second person (using "you/your"), because default prompts and request structure follow this format.

Start playing, chat with your characters, discuss anything you like, build relationships and enjoy the game.

> [!NOTE]
> Depending on the model you are using, your mileage may vary. Do regular backups with `modules/mod-playerbots-characters/tools/pbc_backup.sh` and adjust things as needed in the database (followed by `.chars reload` command). Two other helper tools (`pbc_info.sh` and `pbc_history.sh`) may also help with tracking what's going on.


## Events

Here's the list of possible events that characters could react to.

- **Message received** — fires after the character receives a new message as a whisper or otherwise, for example "John tells you privately: How are you doing?" or "John says: It was a nice fight, huh?"
- **Party found item** — fires when any party member picks up a new item, only for rare (blue) items or higher tiers, for example "The party has found a legendary two-handed mace named Bane of the Damned" or "The party acquired an epic cloth robe named Robes of the Great Arcanist"
- **Character won duel** — fires after the character or someone else in the party wins the duel, for example "John has just won the duel against Joe"
- **Character leveled up** — fires after the character or someone else in the party got a level up in a roleplay-friendly way, for example "John can feel their abilities growing stronger"
- **Character changed location** — fires after the character enters a new location, for example "You have just entered Brill in Tirisfal Glades"
- **Boss slain** — fires when any party member lands the killing blow on a significant opponent (dungeon/raid boss, world boss, or named elite), only when the group contains at least one real player; **always written to all character histories** regardless of whether anyone rolls to respond, for example: "The party has slain Kel'Thuzad (The Lich's Champion) in Naxxramas"
- **Quest taken** — fires when the **party leader** accepts a new quest from an NPC, game object, or item, only when the group contains at least one real player; a preliminary LLM call generates a one-line narrative summary of the quest, for example "The party accepted a task from Gryan Stoutmantle to slay the Defias Brotherhood leader and recover stolen goods."
- **Quest completed** — fires when the **party leader** completes a quest, only when the group contains at least one real player; a preliminary LLM call generates a one-line narrative summary of the quest, for example "The party reported to Gryan Stoutmantle that they successfully slew the Defias Brotherhood leader and recovered the stolen goods."


## Commands

List of commands that can be used by the player or in the server console.

- `.chars reload` — reloads module config, character cards, card additions and character data; also queues a history and relationship reload from the database that runs after all currently pending events are processed (so no in-flight history is lost)
- `.chars condense [char_name]` — forcefully condenses current history, updates character definition and clears current history; also triggers relationship updates for party members that have enough mention data
- `.chars info [char_name]` — prints current character card with historical condensed additions and some basic statistics (number of additions, current number of messages in history, roll chance modifier)
- `.chars reset [char_name]` — removes all historical condensed additions, current chat history and relationship data for the `char_name` character
- `.chars reset @ALL` — removes all historical condensed additions, current chat history and relationship data for all characters, basically restoring the module to its initial state
- `.chars history [char_name] [num=5]` — prints the last `num` entries from the character's in-memory chat history (capped at 20)
- `.chars relationship [char_name] [target_char_name]` — outputs `char_name`'s current LLM-generated relationship description towards `target_char_name`
- `.chars relationship_update [char_name] [target_char_name]` — forcefully queues an immediate relationship update LLM call for `char_name`'s relationship towards `target_char_name`
- `.chars roll_modifier <char_name> [roll_modifier]` — sets or displays the per-character roll chance modifier (integer from -100 to 100). A positive value makes the character more talkative on average (adds to every roll chance), a negative value makes them less talkative (duh). Does not affect whisper or mention reply chances. Omit `roll_modifier` to display the current value.
- `.chars context [char_name]` — builds and prints the current `{context}` variable for the character (defined by `PBC.CharacterContext` with all template variables substituted). Mostly useful for debugging to inspect what context the character would see at the current moment.
- `.chars apitest [query=hi]` — sends a quick test request to the configured LLM API with the system prompt "Answer in one single short sentence." and prints the response (or an error message if the request fails)


## Debugging

Two config options control debug output:

- `PBC.DebugEnabled` — enables general debug logging (event dispatching, roll chances, etc.)
- `PBC.DebugShowFullRequest` — when enabled alongside `DebugEnabled`, logs the full request body and response body for every LLM API call. Useful for diagnosing issues with non-standard API providers where the response format may differ from OpenAI's.


## Variables

### General Variables

These variables can be used in most prompts and character cards. It's recommended to only use things that change often (such as character level or character location) for `PBC.CharacterContext`.

- `{char_name}` — name of the character (simultaneously the bot name as well)
- `{char_gender}` — gender of the character in game
- `{char_race}` — race of the character in game
- `{char_class}` — class of the character in game
- `{char_role}` — character role
- `{char_level}` — level of the character in game
- `{char_gold}` — amount of character's money
- `{char_location}` — human-readable location of the character in game, as a full sentence. For example "You are currently in Undercity." when on the ground, or "You are currently flying to Ratchet, The Barrens." when in a taxi flight.
- `{scene}` — human-readable description of the current time of day, and weather if `mod_weather_vibe` is active, for example "It is currently evening." or "It's currently evening and it's raining lightly."
- `{char_los}` — human-readable list of nearby characters and NPCs visible to the character, for example "You see John, Jane and Defias Bandit nearby." or "You see Defias Bandit nearby."
- `{combat_status}` — dynamic combat status, could be "You are not currently in combat." or "You are currently in combat.", or even "You are currently fighting Archimonde.", based on current target
- `{equipment}` — dynamic equipment description, combining armor quality assessment with weapon details. When bags are at least ~40% full, a bag-space summary is also appended. For example "You have fine equipment made of leather, and wield two rare daggers, called Death's Sting and Deathstriker." or "You have excellent equipment made of plate, and wield an epic two-handed mace called Devastation. Your bags are almost full." or "You have simple equipment, and are unarmed."
- `{char_group}` — dynamic group status, could be "You are not currently in a group." or "You are currently in a group led by John (male Tauren Druid) with the following members: Jane (female Troll Rogue) and Kevin (male Blood Elf Paladin)."


### Main Prompt Variables

These variables can only be used in `PBC.SystemPrompt` and `PBC.UserPrompt`.

- `{character_card}` — current character card or generic description from `PBC.DefaultCharacterDescription` with an addition of previously condensed description
- `{chat_history}` — current chat history, including events
- `{relationships}` — the character's current relationship descriptions with other party members. When the character is not in a group with a real player (e.g. a whisper interaction), falls back to "You don't know much about <player_name>.". When in a group, lists one entry per member, e.g. "You know Luna is brave and kind." or "You don't know much about Jon." for members with no data yet. Updated automatically every `PBC.RelationshipUpdateThreshold` new mentions of a character name in history.
- `{context}` — current context for the character, defined in `PBC.CharacterContext`
- `{event}` — recently happened event, see the Events section above for details


### Quest Prompt Variables

These variables can be used in `PBC.QuestCompletedUserPrompt` and `PBC.QuestTakenUserPrompt`.

- `{quest_title}` — the title of the quest
- `{quest_giver}` — the name of the NPC, game object, or item that offered the quest
- `{quest_ender}` — the name of the NPC or game object that completes the quest
- `{quest_description}` — the full lore/details text of the quest (shown when accepting it)
- `{quest_log_description}` — the objectives text shown in the quest log
- `{quest_completion_log}` — the completion log text
- `{quest_reward_text}` — the NPC's reward speech (OfferRewardText): what the quest-giver says when handing out the reward upon turn-in


## Support & Contributing

Contributions are welcome — feel free to open a pull request. If you need help or found a bug, [open an issue](https://github.com/deseven/mod-playerbots-characters/issues/new).
