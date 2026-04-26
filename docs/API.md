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
| `404` | Requested resource not found (character offline, message/addition/relationship ID out of range) |
| `409` | Desync — `original` text doesn't match server's copy (edit/delete endpoints only) |
| `410` | Authenticated player is not online |


## API Endpoints

### `GET /`

When `PBC.HttpServerFrontendPath` is set and the directory exists, serves `index.html`. Otherwise returns `hello` (text/plain).

### `GET /api/token?otp=XXXXXX`

Exchange a valid one-time password for a bearer token.

| Parameter | Required | Description |
|---|---|---|
| `otp` | Yes | The 6-digit one-time password from `.chars web` |

Returns `{"token": "<bearer_token>"}` on success. Returns `400` if `otp` is missing, `401` if invalid or expired.

### `GET /api/player`

Returns basic info about the authenticated player along with online party members.

```json
{
  "name": "John",
  "gender": "Male",
  "race": "Human",
  "class": "Warrior",
  "level": 80,
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

### `GET /api/char/:guid/card`

Returns the immutable character card (base card with variable substitution, without additions).

### `GET /api/char/:guid/card/additions`

Returns the dynamic card additions for a character as `{"additions": [{"id": 1, "text": "..."}, ...]}`.

### `POST /api/char/:guid/card/additions?id=`

Edit a single card addition. Query param `id` is the 1-based addition ID. Body: `{"text": "New text", "original": "Current text"}`.

### `DELETE /api/char/:guid/card/additions?id=`

Delete a single card addition. Query param `id` is the 1-based addition ID. Body: `{"original": "Current text"}`.

### `GET /api/char/:guid/relationships`

Returns the character's current relationships as `{"relationships": {"John": "...", "Jane": "..."}}`.

### `POST /api/char/:guid/relationships?name=`

Edit a single relationship. Query param `name` is the target character name. Body: `{"text": "New text", "original": "Current text"}`.

### `DELETE /api/char/:guid/relationships?name=`

Delete a single relationship. Query param `name` is the target character name. Body: `{"original": "Current text"}`.

### `GET /api/char/:guid/context`

Returns the fully-built context string for a character with **annotated template variables** — each substituted variable leaves its name before the value (e.g. `{char_name}Jon`). The LLM request uses the non-annotated version.

### `GET /api/history/:guid?page=&limit=`

Returns character chat history. Works even when the character is offline.

| Parameter | Required | Default | Description |
|---|---|---|---|
| `page` | No | `1` | Page number (1-based). Page 1 = most recent. Only used when `limit` is set. |
| `limit` | No | `0` | Messages per page (1–200). Omit or `0` to return all. |

Returns `{"messages": [...], "page", "limit", "total", "total_pages"}`.

### `POST /api/char/:guid/whisper`

Post a private message event for a character. Processed identically to an in-game whisper — the character rolls to respond and the reply is whispered back. Returns immediately with "queued" status. Body: `{"message": "Hello, how are you?"}`.

### `POST /api/history/:guid?id=`

Edit a single message in chat history. Query param `id` is the 1-based message ID. Body: `{"message": "New text", "original": "Current text"}`.

### `DELETE /api/history/:guid?id=`

Delete a single message from chat history. Query param `id` is the 1-based message ID. Body: `{"original": "Current text"}`.


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
| `additions` | Character's card received a new condensed addition | `{"event":"additions"}` |

`thinks`, `relationship`, and `additions` events are simple triggers — fetch updated data via the REST API when received.
