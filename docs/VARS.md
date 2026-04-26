# Variables

## General Variables

These variables can be used in most prompts and character cards. It's recommended to only use things that change often (such as character level or character location) for `PBC.CharacterContext`.

- `{char_name}` — name of the character
- `{char_gender}` — gender of the character in game
- `{char_race}` — race of the character in game
- `{char_class}` — class of the character in game
- `{char_role}` — character role
- `{char_level}` — level of the character in game
- `{char_gold}` — amount of character's money
- `{scene}` — human-readable description of the character's current situation: travel state, location, time of day, and weather (if `mod_weather_vibe` is active). For example "You are currently on foot in Undercity, it's evening." or "You are currently riding Gray Kodo in The Barrens, it's noon." or "You are currently flying to Crossroads, it's morning." or "You are currently on foot in Gadgetzan (Tanaris), it's early evening and it's raining lightly."
- `{char_los}` — human-readable list of nearby characters and NPCs visible to the character, for example "You see John, Jane and Defias Bandit nearby." or "You see Defias Bandit nearby."
- `{combat_status}` — dynamic combat status, could be "You are not currently in combat." or "You are currently in combat.", or even "You are currently fighting Archimonde.", based on current target
- `{equipment}` — dynamic equipment description, combining armor quality assessment with weapon details. When bags are at least ~40% full, a bag-space summary is also appended. For example "You have fine equipment made of leather, and wield two rare daggers, called Death's Sting and Deathstriker." or "You have excellent equipment made of plate, and wield an epic two-handed mace called Devastation. Your bags are almost full." or "You have simple equipment, and are unarmed."
- `{char_group}` — dynamic group status, could be "You are not currently in a group." or "You are currently in a group led by John (male Tauren Druid) with the following members: Jane (female Troll Rogue) and Kevin (male Blood Elf Paladin)."


## Main Prompt Variables

These variables can only be used in `PBC.SystemPrompt` and `PBC.UserPrompt`.

- `{character_card}` — current character card or generic description from `PBC.DefaultCharacterDescription` with an addition of previously condensed description
- `{chat_history}` — current chat history, including events
- `{relationships}` — the character's current relationship descriptions with other party members. When the character is not in a group with a real player (e.g. a whisper interaction), falls back to "You don't know much about <player_name>.". When in a group, lists one entry per member, e.g. "You know John is brave and kind." or "You don't know much about John." for members with no data yet. Updated automatically every `PBC.RelationshipUpdateThreshold` new mentions of a character name in history.
- `{context}` — current context for the character, defined in `PBC.CharacterContext`
- `{event}` — recently happened event, see [Events](EVENTS.md) for details


## Quest Prompt Variables

These variables can be used in `PBC.QuestCompletedUserPrompt` and `PBC.QuestTakenUserPrompt`.

- `{quest_title}` — the title of the quest
- `{quest_giver}` — the name of the NPC, game object, or item that offered the quest
- `{quest_ender}` — the name of the NPC or game object that completes the quest
- `{quest_description}` — the full lore/details text of the quest (shown when accepting it)
- `{quest_log_description}` — the objectives text shown in the quest log
- `{quest_completion_log}` — the completion log text
- `{quest_reward_text}` — the NPC's reward speech: what the quest-giver says when handing out the reward upon turn-in
