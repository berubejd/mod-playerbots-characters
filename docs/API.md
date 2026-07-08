# API Reference

The module can optionally run a built-in HTTP/WS server, providing an API for external tools and integrations. This is disabled by default — see the `HTTP SERVER` section of the config file to enable it.

> [!NOTE]
> The HTTP server itself runs plain HTTP/WS. For production use, place it behind a reverse proxy (e.g. nginx) that handles TLS termination.


## Authorization

All API and WebSocket endpoints (except `GET /` and `/api/token`) require authentication via the `Authorization: Bearer <token>` header. Invalid or missing authentication returns `401`.

1. **Generate an OTP** — use the `.chars web` command in-game to get a 6-digit one-time password (valid for 2 minutes)
2. **Exchange OTP for a token** — `GET /api/token?otp=XXXXXX` returns a bearer token tied to your account
3. **Use the token** — include `Authorization: Bearer <token>` in HTTP requests, or use the `Sec-WebSocket-Protocol` header for WebSocket connections

Tokens are stateless and encrypted using AES-256-CBC. They survive server restarts as long as the private key remains the same. Tokens expire after 1 year.

Once authenticated, you have access to **all characters on your account**.

Endpoints that read or modify character data (history, memories, relationships, data) work for all characters. Endpoints that require the character to be online (context, debug, whisper, trigger) return `404` with a clear error when the target is offline.

Memory and relationship edit/delete endpoints perform **desync detection**: the request must include the current text in the `original` field. If the server's copy differs, `409` with `{"error":"desync"}` is returned. History mutations use real database IDs and do not require desync checks.


## Common Response Codes

Most endpoints share the following response codes:

| Status | Meaning |
|---|---|
| `200` | Success |
| `400` | Missing or invalid parameter, GUID, or request body field |
| `401` | Invalid or missing authentication (except `GET /` and `GET /api/token`) |
| `403` | Character does not belong to your account |
| `404` | Requested resource not found (character offline, message/relationship ID out of range) |
| `409` | Desync — `original` text doesn't match server's copy (memory and relationship edit/delete endpoints only) |


## API Endpoints


### General

#### `GET /`

When `PBC.HttpServerFrontendPath` is set and the directory exists, serves `index.html`. Otherwise returns `hello` (text/plain).

#### `GET /api/token?otp=XXXXXX`

Exchange a valid one-time password for a bearer token.

| Parameter | Required | Description |
|---|---|---|
| `otp` | Yes | The 6-digit one-time password from `.chars web` |

Returns `{"token": "<bearer_token>"}` on success. Returns `400` if `otp` is missing, `401` if invalid or expired.

#### `GET /api/account`

Returns the authenticated account name and all characters on that account.

```json
{
  "account": "myaccount",
  "account_id": 123,
  "characters": [
    {
      "guid": 54321,
      "name": "John",
      "gender": "Male",
      "race": "Human",
      "class": "Warrior",
      "level": 80,
      "is_online": true,
      "is_player": true
    },
    {
      "guid": 12345,
      "name": "Jane",
      "gender": "Female",
      "race": "Night Elf",
      "class": "Priest",
      "level": 78,
      "is_online": true,
      "is_player": false
    },
    {
      "guid": 11111,
      "name": "StorageAlt",
      "gender": "Male",
      "race": "Dwarf",
      "class": "Hunter",
      "level": 5,
      "is_online": false,
      "is_player": false
    }
  ]
}
```

- `account` — account username
- `account_id` — numeric account ID
- `characters` — array of all characters on this account, sorted by name
  - `guid` — character GUID (use with `GET /api/char/:guid/*` endpoints)
  - `name`, `gender`, `race`, `class`, `level` — basic character info
  - `is_online` — whether the character is currently logged in
  - `is_player` — true only when the character is online and is a real (non-bot) player

#### `GET /api/party`

Returns an array of party member GUIDs that belong to the authenticated account. Based on the **real player** being online and in a party.

```json
{
  "party": [12345, 54321]
}
```

- If no real player from the account is online, returns an empty `party` array
- If the real player is not in a party, returns an empty `party` array
- Party members from **different accounts** are excluded — only GUIDs belonging to your account are returned

#### `GET /api/config`

Returns current module configuration parameters.

```json
{
  "config": [
    {"key": "MaxHistoryCtx", "value": 0},
    {"key": "MaxMemoriesCtx", "value": 8192},
    {"key": "ReplyChanceWhisper", "value": 100},
    {"key": "ReplyChanceMention", "value": 100},
    {"key": "ReplyChanceMessage", "value": 100},
    {"key": "RollPenaltyOnAnswer", "value": 45},
    {"key": "ReplyChanceItem", "value": 5},
    {"key": "ReplyChanceDuel", "value": 5},
    {"key": "ReplyChanceLevelUp", "value": 5},
    {"key": "ReplyChanceHardCombat", "value": 25},
    {"key": "ReplyChanceQuestTaken", "value": 10},
    {"key": "ReplyChanceQuestCompleted", "value": 20},
    {"key": "ReplyChanceLocationChanged", "value": 15}
  ]
}
```


### Character Info

#### `GET /api/char/:guid/card`

Returns the character card (base card text with variable placeholders).

#### `GET /api/char/:guid/context`

Returns the fully-built context string for a character with **annotated template variables** — each substituted variable leaves its name before the value (e.g. `[char_name]Jon`). The LLM request uses the non-annotated version.

The character must be online. Returns `404` otherwise.


### Character Chat History

History messages are stored in a normalized `mod_pbc_history` table with a junction table (`mod_pbc_history_owners`) linking characters to messages. A single message may be shared across multiple characters' histories (e.g. party chat).

#### `GET /api/char/:guid/history?page=&limit=`

Returns character chat history with structured message objects.

| Parameter | Required | Default | Description |
|---|---|---|---|
| `page` | No | `1` | Page number (1-based). Page 1 = most recent. Only used when `limit` is set. |
| `limit` | No | `0` | Messages per page (1–200). Omit or `0` to return all. |

Each message object:

| Field | Type | Description |
|---|---|---|
| `id` | uint64 | Real `mod_pbc_history.id` — stable across characters |
| `text` | string | Pre-rendered line from the character's perspective (e.g. `"You: Hello!"`) |
| `author_guid` | uint64 | GUID of the character who spoke (0 = narrator) |
| `type` | uint8 | Chat type: 0=narrator, 2=PARTY, 7=WHISPER (matches `ChatMsg` enum) |
| `message` | string | Raw message text without speaker prefix |
| `author_name` | string | Resolved character name (empty if author is offline/deleted) |

```json
{
  "messages": [
    {
      "id": 42,
      "text": "You: Be careful, there might be traps ahead!",
      "author_guid": 12345,
      "type": 2,
      "message": "Be careful, there might be traps ahead!",
      "author_name": ""
    },
    {
      "id": 41,
      "text": "Narrator: *The party enters a dark cave*",
      "author_guid": 0,
      "type": 0,
      "message": "The party enters a dark cave",
      "author_name": ""
    }
  ],
  "page": 1,
  "limit": 50,
  "total": 150,
  "total_pages": 3
}
```

#### `POST /api/char/:guid/history?id=`

Edit a single message by its real database ID. **Editing affects ALL characters who share this message.**

Query param `id` is the `mod_pbc_history.id` (the `"id"` field from the GET response). Body: `{"message": "New raw message text"}`.

#### `DELETE /api/char/:guid/history?id=`

Delete a message by its real database ID. **This permanently removes the message from ALL characters' histories.** No request body needed.

Query param `id` is the `mod_pbc_history.id`.


### Character Memories

#### `GET /api/char/:guid/memory/count`

Returns the number of memory entries for a character.

```json
{"count": 12}
```

#### `GET /api/char/:guid/memory?order_by=&order_dir=&page=&limit=`

Returns character memories.

| Parameter | Required | Default | Description |
|---|---|---|---|
| `order_by` | No | `id` | Field to sort by: `id`, `memory_text`, or `importance` |
| `order_dir` | No | `desc` (for id/importance), `asc` (for memory_text) | Sort direction: `asc` or `desc` |
| `page` | No | `1` | Page number (1-based). Only used when `limit` is set. |
| `limit` | No | `0` | Items per page (1–200). Omit or `0` to return all. |

Returns `{"memories": [...], "page", "limit", "total", "total_pages"}`. Each memory has `id` (DB row id), `memory_text`, `importance`, and `created_at` (YYYY-MM-DD).

#### `POST /api/char/:guid/memory/:id`

Edit a single memory. The memory is identified by its DB row `id`. Body: `{"memory_text": "New text", "importance": 7, "original": "Current text"}`. If `original` doesn't match, returns `409`.

#### `DELETE /api/char/:guid/memory/:id`

Delete a single memory. The memory is identified by its DB row `id`. Body: `{"original": "Current text"}`. If `original` doesn't match, returns `409`.


### Character Relationships


#### `GET /api/char/:guid/relationships`

Returns the character's current relationships as `{"relationships": {"John": {"text": "...", "updated_at": "YYYY-MM-DD hh:ii:ss"}, ...}}`.

#### `POST /api/char/:guid/relationships?name=`

Edit a single relationship. Query param `name` is the target character name. Body: `{"text": "New text", "original": "Current text"}`.

#### `DELETE /api/char/:guid/relationships?name=`

Delete a single relationship. Query param `name` is the target character name. Body: `{"original": "Current text"}`.


### Character Data

#### `GET /api/char/:guid/data`

Returns character parameters as a JSON array. Currently only `roll_modifier` is supported.

```json
{
  "data": [
    {"key": "roll_modifier", "value": 0}
  ]
}
```

#### `POST /api/char/:guid/data`

Updates character parameters. Accepts the same format as the GET response. Currently only `roll_modifier` is supported (range -100 to 100).

```json
{
  "data": [
    {"key": "roll_modifier", "value": 5}
  ]
}
```


### Debug

#### `GET /api/char/:guid/debug/request?event=`

Returns the generated user prompt for a character as if an event triggered it. Goes through the **exact same logic** normal events use: takes a snapshot, builds the user prompt from the snapshot, and returns both the system prompt and the fully-substituted user prompt. No LLM call is made and no data is modified.

| Parameter | Required | Default | Description |
|---|---|---|---|
| `event` | No | dynamic (based on last history) | Event line text to use as `{event}` in the prompt. Defaults to a context-aware trigger picked from the character's last history message. |

The character must be online. Requires auth.

```json
{
  "system_prompt": "You are a character in...",
  "user_prompt": "You are currently on foot in...",
  "event": "*you feel the urge to say something*",
  "condensation_needed": false,
  "history_tokens": 234,
  "history_token_limit": 8192,
  "memory_tokens": 56,
  "memory_token_limit": 2048,
  "snapshot": {
    "character_card": "...",
    "context": "...",
    "scene": "...",
    "combat_status": "...",
    "equipment": "...",
    "char_group": "...",
    "char_los": "...",
    "memories": "...",
    "relationships": "...",
    "chat_history": "..."
  }
}
```


### Actions

#### `POST /api/char/:guid/whisper`

Post a private message event for a character. Processed identically to an in-game whisper — the character rolls to respond and the reply is whispered back. Returns immediately with "queued" status. Body: `{"message": "Hello, how are you?"}`.

The authenticated account must have a real (non-bot) player online to act as the sender. The target character must be online and belong to your account.

#### `POST /api/char/:guid/narrate`

Add a Narrator line to the specified character's history without producing any character events (no LLM call, no response). Equivalent to the `.chars narrate` command. Triggers a `history` WS event for the character.

Body: `{"message": "A cold wind blows through the forest"}`.

Returns `{"status": "ok"}` on success.

#### `POST /api/party/narrate`

Add a Narrator line to every bot character in the real player's group that belongs to the authenticated account. Equivalent to the `.chars narrate-party` command. Triggers a `history` WS event for each affected character.

The authenticated account must have a real (non-bot) player online and in a group. Characters from other accounts in the same party are excluded.

Body: `{"message": "The party enters a dark cave"}`.

Returns `{"status": "ok", "characters_count": 3}` on success. Returns `400` if no real player is online or no matching characters are in the group.

#### `POST /api/char/:guid/trigger`

Trigger a response from the specified character. The character responds as a party message if they are in a group, or as a say otherwise. The trigger event is not written into the character's history. The event line is picked dynamically based on the character's last history message. No request body required.

The target must be online and must belong to the authenticated account. Returns `403` if the character belongs to a different account.

Returns `{"status": "queued"}` immediately — the actual LLM call and in-game reply happen asynchronously.

#### `POST /api/party/message`

Emulate a party message sent by the authenticated account's real player. The message is added to the history of all characters in the group and goes through the same answer logic as an in-game party chat message (roll chance, LLM call, in-game reply).

The authenticated account must have a real (non-bot) player online and in a group. Returns immediately with "queued" status. Body: `{"message": "Let's move forward"}`.

#### `POST /api/regen-last`

Regenerate the responses of the last event that produced character replies. The same characters respond again in the same order — their original messages are edited in place (message IDs stay stable). No request body required.

Guardrails:
- Only the **last** event is eligible. If it produced no character responses, returns `409`.
- If any new messages were added to an affected character's history since the original event (e.g. narration), the regen is aborted — returns `409` when the guardrail check runs.
- The event queue must be empty; otherwise returns `409`.

Authorization: the authenticated account must have a real (non-bot) player online who is in the same group as at least one of the event's characters. Returns `403` otherwise, `410` if no real player is online.

Returns `{"status": "queued"}` immediately — the actual LLM calls and in-game replies happen asynchronously. A `regen` WebSocket event is sent to all participants when the regeneration completes.

Regeneration can be triggered multiple times — each regen re-runs the same saved event record.


## WebSocket

Connect to `/ws` with the `Sec-WebSocket-Protocol` header for authentication:

```javascript
const ws = new WebSocket('ws://host:8501/ws', ['access_token', token]);
```

Invalid or expired tokens are rejected with `401` before the connection is established.


### Server Messages

| Event | Description |
|---|---|
| `{"event": "connected"}` | Connection established — events are now flowing |
| `{"event": "error", "message": "..."}` | Error |

### Event Types

| Event | Trigger | Payload |
|---|---|---|
| `history` | New message in chat history | `{"event":"history","guid":12345,"message":{"id":42,"text":"You: Hello!","author_guid":12345,"type":2,"message":"Hello!","author_name":"John"}}` |
| `thinks` | Character is about to respond (LLM call starting) | `{"event":"thinks","guid":12345}` |
| `relationship` | Character's relationship updated | `{"event":"relationship","guid":12345}` |
| `memory` | Character received new memories from condensation | `{"event":"memory","guid":12345}` |
| `online` | A character on the account logged in | `{"event":"online","guid":12345}` |
| `offline` | A character on the account logged out | `{"event":"offline","guid":12345}` |
| `party` | Party membership changed for the account | `{"event":"party"}` |
| `regen` | Last event's responses were regenerated | `{"event":"regen","guid":12345,"messages":[{"id":42,"text":"You: Hello again!","type":2,"message":"Hello again!","author_guid":12345,"author_name":"John"}]}` |
| `shutdown` | Server is shutting down | `{"event":"shutdown"}` |

`thinks`, `relationship`, `memory`, `online`, `offline`, and `party` events are simple triggers — fetch updated data via the REST API when received.

`regen` carries an array of updated full message objects (same structure as the `history` event's `message` field) so the frontend can replace the affected history entries in place without a full reload. One `regen` event is sent per participant (each receives the messages rendered from their own perspective).

`shutdown` indicates the server is going down.

> [!NOTE]
> `online`, `offline`, and `party` events may arrive in rapid bursts (e.g. mass logout or group disband). Consumers should batch re-fetches with a short debounce (e.g. 500ms) to avoid redundant API calls.
