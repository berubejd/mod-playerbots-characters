# API Reference

The module can optionally run a built-in HTTP/WS server, providing an API for external tools and integrations. This is disabled by default — see the `HTTP SERVER` section of the config file to enable it.

> [!NOTE]
> The HTTP server itself runs plain HTTP/WS. For production use, place it behind a reverse proxy (e.g. nginx) that handles TLS termination.


## Authorization

All API and WebSocket endpoints (except `GET /` and `/api/token`) require authentication via the `Authorization: Bearer <token>` header. Invalid or missing authentication returns `401`.

1. **Generate an OTP** — use the `.chars web` command in-game to get a 6-digit one-time password (valid for 2 minutes)
2. **Exchange OTP for a token** — `GET /api/token?otp=XXXXXX` returns a bearer token tied to your character
3. **Use the token** — include `Authorization: Bearer <token>` in HTTP requests, or use the `Sec-WebSocket-Protocol` header for WebSocket connections

Tokens are stateless and encrypted using AES-256-CBC. They survive server restarts as long as the private key remains the same. Tokens expire after 30 days.

Most endpoints also require the authenticated player to be online. If the player has logged out, the endpoint returns `410` with `{"error":"player_offline"}`.

Edit and delete endpoints perform **desync detection**: the request must include the current text in the `original` field. If the server's copy differs, `409` with `{"error":"desync"}` is returned.


## Common Response Codes

Most endpoints share the following response codes:

| Status | Meaning |
|---|---|
| `200` | Success |
| `400` | Missing or invalid parameter, GUID, or request body field |
| `401` | Invalid or missing authentication (except `GET /` and `GET /api/token`) |
| `404` | Requested resource not found (character offline, message/relationship ID out of range) |
| `409` | Desync — `original` text doesn't match server's copy (edit/delete endpoints only) |
| `410` | Authenticated player is not online |


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

#### `GET /api/player`

Returns basic info about the authenticated player.

```json
{
  "name": "John",
  "gender": "Male",
  "race": "Human",
  "class": "Warrior",
  "level": 80
}
```

#### `GET /api/party`

Returns online party members for the authenticated player. Requires the player to be online.

```json
{
  "party": [
    {
      "name": "Jane",
      "gender": "Female",
      "race": "Night Elf",
      "class": "Priest",
      "level": 80,
      "character": true,
      "guid": 12345
    }
  ]
}
```

The `party` array contains all online group members (excluding the authenticated player). Each member has a `character` flag — `true` for characters (bots managed by the module), `false` for real players. Characters also include their `guid`.


### Character Info

#### `GET /api/char/:guid/card`

Returns the character card (base card with variable substitution).

#### `GET /api/char/:guid/context`

Returns the fully-built context string for a character with **annotated template variables** — each substituted variable leaves its name before the value (e.g. `{char_name}Jon`). The LLM request uses the non-annotated version.


### Character Chat History

#### `GET /api/char/:guid/history?page=&limit=`

Returns character chat history. Works even when the character is offline.

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

Returns the number of memory entries for a character. Works even when the character is offline.

```json
{"count": 12}
```

#### `GET /api/char/:guid/memory?order_by=&order_dir=&page=&limit=`

Returns character memories. Works even when the character is offline.

| Parameter | Required | Default | Description |
|---|---|---|---|
| `order_by` | No | `id` | Field to sort by: `id`, `memory_text`, or `importance` |
| `order_dir` | No | `desc` (for id/importance), `asc` (for memory_text) | Sort direction: `asc` or `desc` |
| `page` | No | `1` | Page number (1-based). Only used when `limit` is set. |
| `limit` | No | `0` | Items per page (1–200). Omit or `0` to return all. |

Returns `{"memories": [...], "page", "limit", "total", "total_pages"}`. Each memory has `id` (DB row id), `memory_text`, and `importance`.

#### `POST /api/char/:guid/memory/:id`

Edit a single memory. The memory is identified by its DB row `id`. Body: `{"memory_text": "New text", "importance": 7, "original": "Current text"}`. If `original` doesn't match, returns `409`.

#### `DELETE /api/char/:guid/memory/:id`

Delete a single memory. The memory is identified by its DB row `id`. Body: `{"original": "Current text"}`. If `original` doesn't match, returns `409`.


### Character Relationships

#### `GET /api/char/:guid/relationships`

Returns the character's current relationships as `{"relationships": {"John": "...", "Jane": "..."}}`.

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


### Actions

#### `POST /api/char/:guid/whisper`

Post a private message event for a character. Processed identically to an in-game whisper — the character rolls to respond and the reply is whispered back. Returns immediately with "queued" status. Body: `{"message": "Hello, how are you?"}`.

#### `POST /api/char/:guid/narrate`

Add a Narrator line to the specified character's history without producing any character events (no LLM call, no response). Equivalent to the `.chars narrate` command. Triggers a `history` WS event for the character. The character must be online. Body: `{"message": "A cold wind blows through the forest"}`.

Returns `{"status": "ok"}` on success.

#### `POST /api/party/narrate`

Add a Narrator line to every character in the authenticated player's group without producing any character events. Equivalent to the `.chars narrate-party` command. Triggers a `history` WS event for every character in the group. The player must be online and in a group. Body: `{"message": "The party enters a dark cave"}`.

Returns `{"status": "ok", "characters_count": 3}` on success. Returns `400` if the player is not in a group or no characters are in the group.

#### `POST /api/char/:guid/trigger`

Trigger a response from the specified character. The character responds as a party message if they are in a group, or as a say otherwise. The trigger event (`*you feel the urge to say something*`) is not written into the character's history. No request body required. The character must be online.

Returns `{"status": "queued"}` immediately — the actual LLM call and in-game reply happen asynchronously.

#### `POST /api/party/message`

Emulate a party message sent by the authenticated player. The message is added to the history of all characters in the group and goes through the same answer logic as an in-game party chat message (roll chance, LLM call, in-game reply). The player must be online and in a group. Returns immediately with "queued" status. Body: `{"message": "Let's move forward"}`.


## WebSocket

Connect to `/ws` with the `Sec-WebSocket-Protocol` header for authentication:

```javascript
const ws = new WebSocket('ws://host:8501/ws', ['access_token', token]);
```

Invalid or expired tokens are rejected with `401` before the connection is established.

### Event Subscriptions

After connecting, subscribe to real-time events for a specific character:

```
subscribe <GUID>
```

Only one subscription per connection — a second `subscribe` replaces the previous one. To unsubscribe:

```
unsubscribe
```

### Server Messages

| Event | Description |
|---|---|
| `{"event": "connected"}` | Connection established |
| `{"event": "subscribed", "guid": 12345}` | Subscription confirmed |
| `{"event": "unsubscribed"}` | Unsubscribed |
| `{"event": "error", "message": "..."}` | Error |

### Event Types

| Event | Trigger | Payload |
|---|---|---|
| `history` | New message in chat history | `{"event":"history","message":{"id":5,"text":"..."}}` |
| `thinks` | Character is about to respond (LLM call starting) | `{"event":"thinks"}` |
| `relationship` | Character's relationship updated | `{"event":"relationship"}` |
| `memory` | Character received new memories from condensation | `{"event":"memory"}` |

`thinks`, `relationship`, and `memory` events are simple triggers — fetch updated data via the REST API when received.
