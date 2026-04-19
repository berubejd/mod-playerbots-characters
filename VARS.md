# Variables

## General Variables

These variables could be used anywhere, including character cards. It's recommended to only use things that change often (such as character level or character location) for `PBC.CharacterContext` setting.

- `{char_name}` - name of the character (simultaneously the bot name as well)
- `{char_gender}` - gender of the character in game
- `{char_race}` - race of the character in game
- `{char_class}` - class of the character in game
- `{char_role}` - character role
- `{char_level}` - level of the character in game
- `{char_gold}` - amount of character's money
- `{char_location}` - human-readable location of the character in game, as a full sentence. For example "You are currently in Undercity." when on the ground, or "You are currently flying to Ratchet, The Barrens." when in a taxi flight.
- `{scene}` - human-readable description of the current time of day, and weather if `mod_weather_vibe` is active, for example "It is currently evening." or "It's currently evening and it's raining lightly."
- `{char_los}` - human-readable list of nearby characters and NPCs visible to the bot, for example "You see John, Jane and Defias Bandit nearby." or "You see Defias Bandit nearby."
- `{combat_status}` - dynamic combat status, could be "You are not currently in combat." or "You are currently in combat.", or even "You are currently fighting Archimonde.", based on current target
- `{char_group}` - dynamic group status, could be "You are not currently in a group." or "You are currently in a group with John (lvl 10 Tauren Druid), Jane (lvl 11 Troll Rogue) and Kevin (level 10 Blood Elf Paladin)

## Special Variables

These variables can only be used in `PBC.SystemPrompt` and `PBC.UserPrompt`.

- `{character_card}` - current character card or generic description from `PBC.DefaultCharacterDescription` with an addition of previously condensed description
- `{chat_history}` - current chat history, including events
- `{relationships}` - the bot's current relationship descriptions with other party members. When the bot is not in a group with a real player (e.g. a whisper interaction), falls back to "You don't know much about &lt;player_name&gt;.". When in a group, lists one entry per member, e.g. "I know Luna is brave and kind." or "I don't know much about Jon." for members with no data yet. Updated automatically every `PBC.RelationshipUpdateThreshold` new mentions of a character name in history.
- `{context}` - current context for the character, defined in `PBC.CharacterContext`
- `{event}` - recently happened event, see `EVENTS.md` for details

## Quest Completion Prompt Variables

These variables can only be used in `PBC.QuestCompletionUserPrompt`.

- `{quest_title}` - the title of the completed quest
- `{quest_description}` - the full lore/details text of the quest (shown when accepting it)
- `{quest_reward_text}` - the NPC's reward speech (OfferRewardText): what the quest-giver says when handing out the reward upon turn-in