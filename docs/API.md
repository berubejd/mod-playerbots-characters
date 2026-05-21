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

Edit and delete endpoints perform **desync detection**: the request must include the current text in the `original` field. If the server's copy differs, `409` with `{"error":"desync"}` is returned.


## Common Response Codes

Most endpoints share the following response codes:

| Status | Meaning |
|---|---|
| `200` | Success |
| `400` | Missing or invalid parameter, GUID, or request body field |
| `401` | Invalid or missing authentication (except `GET /` and `GET /api/token`) |
| `403` | Character does not belong to your account |
| `404` | Requested resource not found (character offline, message/relationship ID out of range) |
| `409` | Desync — `original` text doesn't match server's copy (edit/delete endpoints only) |


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
    {"key": "TrackPlayerCharacter", "value": false},
    {"key": "MaxResponseLength", "value": 120},
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

#### `GET /api/char/:guid/history?page=&limit=`

Returns character chat history.

| Parameter | Required | Default | Description |
|---|---|---|---|
| `page` | No | `1` | Page number (1-based). Page 1 = most recent. Only used when `limit` is set. |
| `limit` | No | `0` | Messages per page (1–200). Omit or `0` to return all. |

Returns `{"messages": [...], "page", "limit", "total", "total_pages"}`.

#### `POST /api/char/:guid/history?id=`

Edit a single message in chat history. Query param `id` is the 1-based message ID. Body: `{"message": "New text", "original": "Current text"}`.

#### `DELETE /api/char/:guid/history?id=`

Delete a single message from chat history. Query param `id` is the 1-based message ID. Body: `{"original": "Current text"}`.


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
| `event` | No | `*you feel the urge to say something*` | Event line text to use as `{event}` in the prompt |

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

Trigger a response from the specified character. The character responds as a party message if they are in a group, or as a say otherwise. The trigger event (`*you feel the urge to say something*`) is not written into the character's history. No request body required.

The target must be online and must belong to the authenticated account. Returns `403` if the character belongs to a different account.

Returns `{"status": "queued"}` immediately — the actual LLM call and in-game reply happen asynchronously.

#### `POST /api/party/message`

Emulate a party message sent by the authenticated account's real player. The message is added to the history of all characters in the group and goes through the same answer logic as an in-game party chat message (roll chance, LLM call, in-game reply).

The authenticated account must have a real (non-bot) player online and in a group. Returns immediately with "queued" status. Body: `{"message": "Let's move forward"}`.


## WebSocket

Connect to `/ws` with the `Sec-WebSocket-Protocol` header for authentication:

```javascript
const ws = new WebSocket('ws://host:8501/ws', ['access_token', token]);
```

Invalid or expired tokens are rejected with `401` before the connection is established.

### Event Subscriptions

After connecting, send `subscribe` to start receiving real-time events for all characters on your account:

```
subscribe
```

Events are delivered for every character on the authenticated account. To unsubscribe:

```
unsubscribe
```

### Server Messages

| Event | Description |
|---|---|
| `{"event": "connected"}` | Connection established |
| `{"event": "subscribed"}` | Subscription confirmed |
| `{"event": "unsubscribed"}` | Unsubscribed |
| `{"event": "error", "message": "..."}` | Error |

### Event Types

| Event | Trigger | Payload |
|---|---|---|
| `history` | New message in chat history | `{"event":"history","guid":12345,"message":{"id":5,"text":"..."}}` |
| `thinks` | Character is about to respond (LLM call starting) | `{"event":"thinks","guid":12345}` |
| `relationship` | Character's relationship updated | `{"event":"relationship","guid":12345}` |
| `memory` | Character received new memories from condensation | `{"event":"memory","guid":12345}` |
| `online` | A character on the account logged in | `{"event":"online","guid":12345}` |
| `offline` | A character on the account logged out | `{"event":"offline","guid":12345}` |
| `party` | Party membership changed for the account | `{"event":"party"}` |
| `shutdown` | Server is shutting down | `{"event":"shutdown"}` |

`thinks`, `relationship`, `memory`, `online`, `offline`, and `party` events are simple triggers — fetch updated data via the REST API when received.

`shutdown` indicates the server is going down.

> [!NOTE]
> `online`, `offline`, and `party` events may arrive in rapid bursts (e.g. mass logout or group disband). Consumers should batch re-fetches with a short debounce (e.g. 500ms) to avoid redundant API calls.
