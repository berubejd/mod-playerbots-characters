# Playerbots Characters (PBC)

This is an [AzerothCore](https://www.azerothcore.org) module built around [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots), breathing new life into bots by turning them into true in-game characters — companions with pre-defined personalities, memory, and relationships. Heavily inspired by [mod-ollama-chat](https://github.com/DustinHendrickson/mod-ollama-chat), but taking a different, more complex approach — focusing on the roleplaying experience rather than emulating real WoW players.

Think old Bioware games with companions — that's the core idea. The intended use is a fresh start at low rates with a full party of altbots playing alongside you, developing their own stories as you progress through the game together.


## How It Works

This module extends the bots provided by `mod-playerbots` into full characters — giving them a voice, memories, relationships, and the ability to react to events and converse with companions. The bot logic (combat, movement, questing) remains entirely untouched; this module only adds the personality layer on top.

Characters react to in-game events (chat, item pickups, duels, level-ups, boss kills, quests) based on configurable reply chances. When a character rolls to respond, a prompt is built from its character card, chat history, relationships, live context, and the event itself — then sent to an OpenAI-compatible LLM API, and the response is spoken by the character in-game.

Over time, chat history grows. When it reaches the configured token limit, a condensation process summarizes the history and appends it to the character's card as a permanent addition, keeping the in-memory context bounded. This way, characters gradually develop memories and personality traits.

Relationships are tracked between characters and real players, as well as other characters. When a name is mentioned enough times in a character's history, a relationship update is triggered — generating or updating a brief description of how the character feels about that person. These are included in future prompts, giving characters continuity with their companions.


## Installation

Since mod-playerbots is an obvious hard requirement, follow their [Installation Guide](https://github.com/mod-playerbots/mod-playerbots/wiki/Installation-Guide) until you have a working acore installation with playerbots enabled.

Next, clone this repository into the `modules` directory of your acore sources and rebuild the server normally.

> [!NOTE]
> 1. Only Linux is officially supported as a build target. Technically nothing should stop you from using the module on Windows, but this is untested and unsupported.
> 2. The module includes bundled copies of [nlohmann/json](https://github.com/nlohmann/json) in `deps/nlohmann/json.hpp` and [cpp-httplib](https://github.com/yhirose/cpp-httplib) in `deps/yhirose/cpp-httplib/httplib.h`, so no external libraries are required for these. The build system will use the bundled versions by default, falling back to a system-installed version of nlohmann/json if the bundled one is not present.

If [mod_weather_vibe](https://github.com/hermensbas/mod_weather_vibe) is also installed, weather states from it will be included in the character's scene description.


## Configuration

Copy `env/dist/etc/modules/playerbots_characters.conf.dist` as `env/dist/etc/modules/playerbots_characters.conf` and adjust it as needed.


### Model Connection Setup

The module supports two API formats, controlled by `PBC.APIType`:

- **`openai`** (default) — OpenAI-compatible `/chat/completions` endpoint. The module appends `/chat/completions` to `PBC.BaseUrl` and sends the API key via `Authorization: Bearer` header.
- **`anthropic`** — Anthropic Messages API `/messages` endpoint. The module appends `/messages` to `PBC.BaseUrl` and sends the API key via `x-api-key` header with the `anthropic-version: 2023-06-01` header.

You need to configure at least `PBC.BaseUrl`, `PBC.Model` and `PBC.ApiKey` before the module can generate responses. The relevant config options are in the **API CONNECTION** and **MODEL PARAMETERS** sections of the config file. After configuring, you can use `.chars api-test` to quickly verify that the connection is working (or `.chars alt-api-test` for the alternative model).

> [!IMPORTANT]
> Due to the complexity and length of the prompts, locally-run models on average home hardware will generally struggle and produce low-quality output as context grows. A cloud-based model with a large context window is recommended. Make sure to also adjust `PBC.MaxCtx` accordingly — a good starting point is around 25% of the model's total context window. Aim for at least 32k in general, anything less could lead to poor efficiency of character relationship tracking and card additions.

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

### Alternative Model

Condensation and relationship updates are critical — their output becomes permanent parts of the character's context. The `ALTERNATIVE API` config section lets you route these tasks to a more capable model while keeping the main chat on a cheaper/faster one. Parameters follow the same format as the main model, prefixed with `AltModel`.

### HTTP Server & Web App

The module can optionally run a built-in HTTP/WS server with a web frontend, providing an interface for managing the characters, as well as an API for external tools and integrations. The web interface supports viewing and editing chat history, card additions, and relationships. This is disabled by default and could be enabled in the config, read the `HTTP SERVER` section and follow the comments there.

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


## Usage

Start the server, set up some altbots for yourself or invite existing random bots, then write cards for them as you see fit. See `characters/Example.card.txt` for an example of how to write a character card. The character name in the filename must match the in-game character name exactly to be picked up. It is recommended to write cards in second person (using "you/your"), because default prompts and request structure follow this format.

Start playing, chat with your characters, discuss anything you like, build relationships and enjoy the game.

> [!NOTE]
> Depending on the model you are using, your mileage may vary. Do regular backups with `modules/mod-playerbots-characters/tools/pbc_backup.sh` and adjust things as needed either in the database (followed by `.chars reload` command) or via the included web app. There are also two helper tools (`pbc_info.sh` and `pbc_history.sh`) which might help with tracking what's going on. You can also steer the narration a bit by using `.chars narrate` and `.chars narrate-group` commands. Check out [available commands](docs/COMMANDS.md) for more info.

### API & Web App

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
