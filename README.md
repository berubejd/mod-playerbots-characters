# Playerbots Characters (PBC)

This is an [AzerothCore](https://www.azerothcore.org) module built around [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots), breathing new life into bots by turning them into true in-game characters — companions with pre-defined personalities, memory, and relationships. Heavily inspired by [mod-ollama-chat](https://github.com/DustinHendrickson/mod-ollama-chat), but taking a different, more complex approach — focusing on the roleplaying experience rather than emulating real WoW players.

Think old Bioware games with companions — that's the core idea. The intended use is a fresh start at low rates with a full party of altbots playing alongside you, developing their own stories as you progress through the game together.

> [!IMPORTANT]
> The module is currently in active development and things could be changing rapidly. Before updating your copy, it's highly recommended to do a database backup, read the changes and notes in [the Releases](https://github.com/deseven/mod-playerbots-characters/releases) and check your server logs after running the newer version. There should be no hard incompatibilities, but new config variables are getting added, existing ones change their defaults, old routines get replaced and so on.


## How It Works

This module extends the bots provided by `mod-playerbots` into full characters — giving them a voice, memories, relationships, and the ability to react to events and converse with companions. The bot logic (combat, movement, questing) remains entirely untouched; this module only adds the personality layer on top.

Characters react to in-game events (chat, item pickups, duels, level-ups, boss kills, quests) based on configurable reply chances. When a character rolls to respond, a prompt is built from its character card, chat history, relationships, live context, and the event itself — then sent to a compatible LLM API, and the response is spoken by the character in-game.

Over time, chat history grows. When it reaches the configured token limit, a condensation process extracts discrete narrator-style memories with importance scores from the history, then clears all history. These memories are selected by importance at prompt-build time within a token budget, keeping the in-memory context bounded while preserving what matters. This way, characters gradually develop lasting memories and personality traits.

Relationships are tracked between characters and real players, as well as other characters. When a name is mentioned enough times in a character's history, a relationship update is triggered — generating or updating a brief description of how the character feels about that person. These are included in future prompts, giving characters continuity with their companions.

The module supports multiple languages based on the server's `DBC.Locale` setting:
- enUS (default, picked if the needed locale is not found)
- deDE
- ruRU


## Installation

Since mod-playerbots is an obvious hard requirement, follow their [Installation Guide](https://github.com/mod-playerbots/mod-playerbots/wiki/Installation-Guide) until you have a working acore installation with playerbots enabled.

There are three ways to obtain the module sources:

### Method 1: Specific release tag (recommended)

Clone a specific tagged release:

```sh
cd modules
git clone --depth 1 --branch <tag_name> git@github.com:deseven/mod-playerbots-characters.git
```

When you want to upgrade to a newer release tag:

```sh
cd modules/mod-playerbots-characters
git fetch --tags
git checkout <new_tag_name>
```

A list of all available tags can be found on the [Releases](https://github.com/deseven/mod-playerbots-characters/releases) page.

### Method 2: Source code archive

Download a `.zip` or `.tar.gz` source archive from the [Releases](https://github.com/deseven/mod-playerbots-characters/releases) page and extract it into the `modules` directory. The extracted folder must be named `mod-playerbots-characters`.

### Method 3: Latest (bleeding edge)

Clone the repository normally to get the latest changes:

```sh
cd modules
git clone git@github.com:deseven/mod-playerbots-characters.git
```

This gives you the most recent commits from the `main` branch. You can `git pull` at any time to update.

After obtaining the sources using any of the methods above, rebuild the server normally.

> [!NOTE]
> The module includes bundled copies of [nlohmann/json](https://github.com/nlohmann/json) in `deps/nlohmann/json.hpp` and [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) in `deps/yhirose/cpp-httplib/httplib.h`, so no external libraries are required.

If [mod_weather_vibe](https://github.com/hermensbas/mod_weather_vibe) is also installed, weather states from it will be included in the character's scene description.


## Configuration

Copy `env/dist/etc/modules/playerbots_characters.conf.dist` as `env/dist/etc/modules/playerbots_characters.conf` and adjust it as needed.


### Model Connection Setup

The module supports three API formats, controlled by `PBC.APIType`:

- **`openai`** (default) — OpenAI-compatible `/chat/completions` endpoint. The module appends `/chat/completions` to `PBC.BaseUrl` and sends the API key via `Authorization: Bearer` header.
- **`anthropic`** — Anthropic Messages API `/messages` endpoint. The module appends `/messages` to `PBC.BaseUrl` and sends the API key via `x-api-key` header with the `anthropic-version: 2023-06-01` header.
- **`ollama`** — Ollama native `/api/chat` endpoint. The module appends `/api/chat` to `PBC.BaseUrl` and exposes Ollama-specific controls (`think`, `keep_alive`, `num_ctx`, `num_predict`) that the OpenAI-compatible shim hides. Recommended for local Ollama instances — `think:false` suppresses the reasoning tokens that otherwise dominate latency. See [Ollama (local)](#ollama-local) below.

You need to configure at least `PBC.BaseUrl`, `PBC.Model` and `PBC.ApiKey` before the module can generate responses. The relevant config options are in the **API CONNECTION** and **MODEL PARAMETERS** sections of the config file. After configuring, you can use `.chars api-test` to quickly verify that the connection is working (or `.chars alt-api-test` for the alternative model).

> [!IMPORTANT]
> Due to the complexity and length of the prompts, locally-run models on average home hardware will generally struggle and produce low-quality output as context grows. A cloud-based model with a large context window is recommended. Make sure to also adjust `PBC.MaxHistoryCtx` accordingly — a good starting point is around 25% of the model's total context window. Aim for at least 32k in general, anything less could lead to poor efficiency of character relationship tracking and memory extraction.

Choosing the right model can be tricky. Two tested configurations are listed below.

#### DeepSeek

| Setting | Value |
|---|---|
| `PBC.APIType` | `openai` |
| `PBC.BaseUrl` | `https://api.deepseek.com/v1` |
| `PBC.Model` | `deepseek-v4-pro` |
| `PBC.Temperature` | `1.0` |
| `PBC.MaxHistoryCtx` | `32768` |
| `PBC.MaxMemoriesCtx` | `8192` |
| `PBC.ModelExtraParameters` | `'thinking':{'type':'disabled'}` |
| `PBC.ApiKey` | your API key from [DeepSeek platform](https://platform.deepseek.com/) |

DeepSeek offers a reasonable cost/capabilities compromise and can be considered the cheapest viable option. Expect to spend under $1 for several hours of play.

#### GLM (Zhipu AI)

| Setting | Value |
|---|---|
| `PBC.APIType` | `openai` |
| `PBC.BaseUrl` | `https://api.z.ai/api/paas/v4` |
| `PBC.Model` | `glm-5.1` |
| `PBC.Temperature` | `1.0` |
| `PBC.MaxHistoryCtx` | `32768` |
| `PBC.MaxMemoriesCtx` | `8192` |
| `PBC.ModelExtraParameters` | `'thinking':{'type':'disabled'}` |
| `PBC.ApiKey` | your API key from [Z.ai](https://z.ai/manage-apikey/apikey-list) |

GLM 5.1 handles the required tasks impressively well, though the cost adds up fairly quickly. Expect to spend around $2 per long game session with a full party.

#### Ollama (local)

For a local [Ollama](https://ollama.com/) instance, use the native `ollama` API type rather than the OpenAI-compatible shim — it lets you suppress reasoning tokens and pin the context window directly.

| Setting | Value |
|---|---|
| `PBC.APIType` | `ollama` |
| `PBC.BaseUrl` | `http://127.0.0.1:11434` |
| `PBC.Model` | `gemma3n:e4b` |
| `PBC.ApiKey` | (leave empty) |
| `PBC.Temperature` | `1.0` |
| `PBC.MaxResponseLength` | `120` |
| `PBC.MaxHistoryCtx` | `32768` |
| `PBC.OllamaThink` | `0` |
| `PBC.OllamaKeepAlive` | `-1` |
| `PBC.OllamaNumCtx` | `8192` |

The Ollama-specific options live in the **OLLAMA OPTIONS** section of the config:

- **`PBC.OllamaThink`** — `0` sends `think:false`, disabling the reasoning output that is the main latency source for local models. Set to `1` only for models that support thinking.
- **`PBC.OllamaKeepAlive`** — how long the model stays resident after a request. `-1` keeps it loaded indefinitely (pairs well with GPU pinning); leave empty for Ollama's default.
- **`PBC.OllamaNumCtx`** — pins the request context window (`options.num_ctx`) so local memory use stays bounded. This is the model's context window and is separate from `PBC.MaxHistoryCtx`, which governs in-prompt history condensation.
- **`PBC.MaxResponseLength`** drives `options.num_predict` (output cap), exactly as it does for the other API types.

> [!NOTE]
> The general caveat about locally-run models still applies — as context grows, smaller local models produce lower-quality output than frontier cloud models. The `ollama` type makes local inference viable and predictable, not equivalent to a large cloud model.

For sampling knobs, remember that Ollama only honors them inside the `options` object. Put them in `PBC.OllamaExtraOptions` (e.g. `'top_p':0.9,'repeat_penalty':1.1`), **not** `PBC.ModelExtraParameters` — the latter splices at the top level of the request body (correct for fields like `format` or `tools`, but ignored by Ollama for sampling parameters).

#### Other Models

Any OpenAI-compatible API should work with `PBC.APIType = openai` — just set `PBC.BaseUrl` to the endpoint (the module appends `/chat/completions` automatically), `PBC.Model` to the model identifier, and `PBC.ApiKey` to your bearer token. If the endpoint doesn't require authentication (e.g. an LM Studio instance, or Ollama's OpenAI-compatible shim), leave `PBC.ApiKey` empty. For Ollama specifically, prefer the native [`ollama`](#ollama-local) API type over the OpenAI-compatible shim.

Use `PBC.ModelExtraParameters` to inject provider-specific JSON into the request body — this works the same way for the `openai`, `anthropic`, and `ollama` API types (for `ollama` it splices at the top level; use `PBC.OllamaExtraOptions` for sampling knobs). Make sure to use parameter names that are valid for your chosen API type (e.g. `top_p` and `top_k` for Anthropic, `frequency_penalty` and `presence_penalty` for OpenAI-compatible providers). Single quotes are used instead of double quotes and are automatically replaced at runtime:

```
PBC.ModelExtraParameters = 'frequency_penalty':0.5,'presence_penalty':0.2
```

becomes `"frequency_penalty":0.5,"presence_penalty":0.2` in the request.

When switching providers, pay attention to `PBC.Temperature` — acceptable ranges vary between models. Check the provider's documentation for the recommended value.

### Alternative Model

Condensation and relationship updates are critical — their output becomes permanent parts of the character's context. The `ALTERNATIVE API` config section lets you route these tasks to a more capable model while keeping the main chat on a cheaper/faster one. Parameters follow the same format as the main model, prefixed with `AltModel`.

You can also enable thinking mode (if the model supports it of course) to improve the results further.

### HTTP Server & Web App

![PBC Web App](https://d7.wtf/s/pbc-web.png)

The module can optionally run a built-in HTTP/WS server with a web frontend, providing an interface for managing the characters, as well as an API for external tools and integrations. The web interface supports viewing and editing chat history, relationships and memories. This is disabled by default and could be enabled in the config, read the `HTTP SERVER` section and follow the comments there.

> [!NOTE]
> If you plan to use the web app for a considerable amount of time without touching the game, it's also recommended to set `PreventAFKLogout` to `2` in your `worldserver.conf`.

### Playerbots Adjustments

Recommended adjustments to the playerbots config (`playerbots.conf`), to make bots less talkative in order to not interfere with the new character logic:

| Setting | Value | Purpose |
|---|---|---|
| `AiPlayerbot.EnableBroadcasts` | 0 | Disables loot / quest / kill broadcasts |
| `AiPlayerbot.RandomBotTalk` | 0 | Disables random talking in say / yell / general channels |
| `AiPlayerbot.RandomBotEmote` | 0 | Disables random emoting |
| `AiPlayerbot.RandomBotSuggestDungeons` | 0 | Disables dungeon suggestions in chat |
| `AiPlayerbot.EnableGreet` | 0 | Disables greeting when invited |
| `AiPlayerbot.GuildFeedback` | 0 | Disables guild event chatting |
| `AiPlayerbot.RandomBotSayWithoutMaster` | 0 | Disables bots talking without a master |
| `AiPlayerbot.RPWarningCooldown` | 999999 | Increases delay between "missing reagents" messages and such |
| `AiPlayerbot.EnableAutoTradeOnItemMention` | 0 | Disables automatic trades and item listings on item mention (from [mod-playerbots@8caf37a](https://github.com/mod-playerbots/mod-playerbots/commit/8caf37af97b74545f8fa65172095803466ad8a06)) |


## Usage

Start the server, set up some altbots for yourself or invite existing random bots, then write cards for them as you see fit. See `characters/Example.card.txt` for an example of how to write a character card. The character name in the filename must match the in-game character name exactly to be picked up. It is recommended to write cards in second person (using "you/your"), because default prompts and request structure follow this format.

Start playing, chat with your characters, discuss anything you like, build relationships and enjoy the game.

> [!NOTE]
> Depending on the model you are using, your mileage may vary. Do regular backups with `modules/mod-playerbots-characters/tools/pbc_backup.sh` and adjust things as needed either in the database (followed by `.chars reload` command) or via the included web app. There are also two helper tools (`pbc_info.sh` and `pbc_history.sh`) which might help with tracking what's going on. You can also steer the narration a bit by using `.chars narrate` and `.chars narrate-party` commands. Check out [available commands](docs/COMMANDS.md) for more info.

### Web App

If you have the HTTP server configured, you can use `.chars web` command to get a one-time password that you can then use to authorize in the web app.

## Debugging

Two config options control debug output:

- `PBC.DebugEnabled` — enables general debug logging (event dispatching, roll chances, etc.)
- `PBC.DebugShowFullRequest` — when enabled alongside `DebugEnabled`, logs the full request body and response body for every LLM API call. Useful for diagnosing issues with non-standard API providers where the response format may differ from OpenAI's.


## Additional Documentation

- [Events](docs/EVENTS.md) — list of in-game events characters can react to
- [Commands](docs/COMMANDS.md) — available in-game and console commands
- [Prompts](docs/PROMPTS.md) — prompt templates, customization and template variables
- [API Reference](docs/API.md) — HTTP API and WebSocket documentation
- [Frontend](frontend/README.md) — web interface build and development


## Support & Contributing

Contributions are welcome — feel free to open a pull request. If you need help or found a bug, [open an issue](https://github.com/deseven/mod-playerbots-characters/issues/new).
