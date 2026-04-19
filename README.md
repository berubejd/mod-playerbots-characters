# mod-playerbots-characters

A character-focused LLM integration for [AzerothCore](https://www.azerothcore.org/) playerbots.

Bots react to in-game chat and events using any **OpenAI-compatible API** (DeepSeek, OpenAI, local Ollama, LM Studio, etc.). Each bot can have a **character card** — a plain text file that defines their personality, backstory, and speech style. Conversation history is kept in memory, periodically saved to the database, and automatically **condensed** into the character card when the context window fills up.

## Requirements

- AzerothCore with the [mod-playerbots](https://github.com/liyunfan1223/mod-playerbots) module.
- An OpenAI-compatible API endpoint (local or remote).
- C++17 compiler (for `std::filesystem`).
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — place `httplib.h` in `src/httplib.h`. You can copy it from `mod-ollama-chat/src/httplib.h` if that module is present.
- [nlohmann/json](https://github.com/nlohmann/json) — place `json.hpp` in `deps/nlohmann/json.hpp`, or install system-wide.
- OpenSSL development libraries (for HTTPS): `apt install libssl-dev`.

## Installation

1. Clone or copy this module into `modules/mod-playerbots-characters/`.
2. Copy `httplib.h` into `src/httplib.h` (see above).
3. Run `build_server.sh` to rebuild.
4. Source the SQL files in order:

```sql
SOURCE modules/mod-playerbots-characters/data/sql/characters/base/2026_04_15_01_character_cards.sql;
SOURCE modules/mod-playerbots-characters/data/sql/characters/base/2026_04_15_02_chat_history.sql;
```

5. Copy `conf/playerbots_characters.conf.dist` to your server's `conf/` directory as `playerbots_characters.conf` and edit it.
6. Create character cards (see below).
7. Restart the worldserver.

## Character Cards

Place `.txt` files in the directory configured by `PBC.CharacterCardsPath` (default: `modules/mod-playerbots-characters/characters/`).

The filename (without extension) must match the **bot's in-game name** (case-insensitive).

Examples:
- `thrall.txt` → bot named "Thrall"
- `sylvanas.card.txt` → bot named "Sylvanas"

The file content is free-form text describing the character's personality, backstory, speech quirks, relationships, etc.

### Sample card (`thrall.txt`)

```
You are Thrall, Warchief of the Horde. You carry the weight of your people's survival on your shoulders.
You speak with calm authority, choosing words carefully. You respect strength and honour above all.
You have a deep bond with the elements and the shamanic traditions of the Frostwolf clan.
You distrust the Burning Legion and the undead, but you seek peace when possible.
You speak Common with a slight Orcish directness. You do not mince words.
```

If no card file is found for a bot, `PBC.DefaultCharacterDescription` is used instead, with `{char_gender}`, `{char_race}`, and `{char_class}` substituted automatically.

## Configuration

See [`conf/playerbots_characters.conf.dist`](conf/playerbots_characters.conf.dist) for all options with descriptions.

Key settings:

| Key | Default | Description |
|-----|---------|-------------|
| `PBC.Enable` | `1` | Enable/disable the module |
| `PBC.BaseUrl` | `https://api.deepseek.com/v1` | OpenAI-compatible API base URL |
| `PBC.ApiKey` | *(empty)* | Bearer token; leave empty for local APIs |
| `PBC.Model` | `deepseek-chat` | Model identifier |
| `PBC.MaxCtx` | `32768` | Token budget per bot before condensation |
| `PBC.MaxConcurrentRequests` | `3` | Global cap on simultaneous API calls |
| `PBC.RequestTimeoutSec` | `30` | HTTP request timeout |
| `PBC.HistorySaveIntervalSec` | `300` | How often history is flushed to DB (5 min) |

## How it works

### History

Each bot maintains a per-bot ordered list of pre-formatted lines:

```
John: Hello, how are you?
Thrall: Well met, John. The Horde endures.
*John picked up [Thunderfury, Blessed Blade of the Windseeker]*
*You died fighting Onyxia*
```

History is kept in memory and written to the `mod_pbc_chat_history` table every `HistorySaveIntervalSec` seconds and on server shutdown.

### Condensation

When the estimated token count of a bot's history exceeds `MaxCtx`, the module:

1. Calls the LLM with the condensation prompts to produce a summary of recent events.
2. Appends the summary to `mod_pbc_character_card_additions` in the database.
3. Clears the bot's current history.

The next time a prompt is built for that bot, the summary is appended to the base character card automatically.

### Concurrency

- A global atomic counter limits simultaneous requests to `MaxConcurrentRequests`.
- A per-bot set prevents a bot from having two overlapping requests.
- When either limit is hit, the LLM call is skipped (the history line is still recorded).
- Bot-to-bot reply chains use `ReplyChanceMessage` (not the higher `ReplyChanceMention`), naturally limiting cascade depth.

## Commands

All commands require GM level.

| Command | Description |
|---------|-------------|
| `.chars reload` | Reload config and character cards from disk |
| `.chars info [name]` | Show card, addition count, history stats |
| `.chars condense [name]` | Force condensation of a bot's history |
| `.chars reset [name]` | Clear all history and card additions for a bot |

## Events bots react to

| Event | History line format |
|-------|---------------------|
| Chat (say/group/whisper) | `Name: message` |
| Player killed by creature | `*Name died fighting CreatureName*` |
| Rare+ item looted | `*Name picked up [ItemName]*` |
| Duel won | `*Winner won the duel against Loser*` |
| Level up | `*Name leveled up and is now level N*` |
| Bot enters new area | `*You have just entered AreaName*` |

## Database tables

| Table | Purpose |
|-------|---------|
| `mod_pbc_chat_history` | Per-bot ordered chat/event history lines |
| `mod_pbc_character_card_additions` | LLM-condensed summaries appended to character cards |

## Template variables

See [`VARS.md`](VARS.md) for the full list of `{variable}` placeholders available in prompts and character cards.

See [`EVENTS.md`](EVENTS.md) for the list of events bots react to.

See [`COMMANDS.md`](COMMANDS.md) for in-game command reference.
