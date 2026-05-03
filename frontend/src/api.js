// Token management has moved to account-store.js (multi-account support).
// ---------------------------------------------------------------------------

// HTTP status → error tag mapping helper
//
// Checks the response status against the provided map and throws the
// corresponding Error.  Falls back to a generic 'error' for any unhandled
// non-ok status.  This eliminates the repeated if-chains in every API
// function.
// ---------------------------------------------------------------------------
function throwForStatus(res, statusMap = {}) {
  const error = statusMap[res.status];
  if (error) throw new Error(error);
  if (!res.ok) throw new Error('error');
}

// Common status maps shared across endpoints
const AUTH_STATUS = { 401: 'unauthorized', 410: 'player_offline' };
const CHAR_STATUS = { ...AUTH_STATUS, 404: 'offline', 400: 'not_character' };
const MUTATE_STATUS = { ...AUTH_STATUS, 400: 'bad_request', 404: 'not_found', 409: 'desync' };

export async function exchangeOtp(otp) {
  const res = await fetch(`/api/token?otp=${encodeURIComponent(otp)}`);
  if (res.status === 401) throw new Error('invalid');
  if (!res.ok) throw new Error('error');
  const data = await res.json();
  return data.token;
}

export async function fetchPlayer(token) {
  const res = await fetch('/api/player', {
    headers: { Authorization: `Bearer ${token}` },
  });
  // /api/player has its own online check returning 404
  throwForStatus(res, { 401: 'unauthorized', 404: 'offline' });
  return res.json();
}

export async function fetchCard(token, guid) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/card`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_STATUS);
  return res.json();
}

export async function fetchCardAdditions(token, guid) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/card/additions`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_STATUS);
  return res.json();
}

export async function fetchRelationships(token, guid) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/relationships`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_STATUS);
  return res.json();
}

export async function fetchContext(token, guid) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/context`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_STATUS);
  return res.json();
}

export async function fetchHistory(token, guid) {
  const res = await fetch(`/api/history/${encodeURIComponent(guid)}`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, AUTH_STATUS);
  return res.json();
}

export async function sendWhisper(token, guid, message) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/whisper`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ message }),
  });
  throwForStatus(res, { ...AUTH_STATUS, 400: 'bad_request', 404: 'offline' });
  return res.json();
}

export async function editMessage(token, guid, id, message, original) {
  const res = await fetch(`/api/history/${encodeURIComponent(guid)}?id=${encodeURIComponent(id)}`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ message, original }),
  });
  throwForStatus(res, MUTATE_STATUS);
  return res.json();
}

export async function deleteMessage(token, guid, id, original) {
  const res = await fetch(`/api/history/${encodeURIComponent(guid)}?id=${encodeURIComponent(id)}`, {
    method: 'DELETE',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ original }),
  });
  throwForStatus(res, MUTATE_STATUS);
  return res.json();
}

export async function editCardAddition(token, guid, id, text, original) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/card/additions?id=${encodeURIComponent(id)}`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ text, original }),
  });
  throwForStatus(res, MUTATE_STATUS);
  return res.json();
}

export async function deleteCardAddition(token, guid, id, original) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/card/additions?id=${encodeURIComponent(id)}`, {
    method: 'DELETE',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ original }),
  });
  throwForStatus(res, MUTATE_STATUS);
  return res.json();
}

export async function editRelationship(token, guid, name, text, original) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/relationships?name=${encodeURIComponent(name)}`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ text, original }),
  });
  throwForStatus(res, MUTATE_STATUS);
  return res.json();
}

export async function deleteRelationship(token, guid, name, original) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/relationships?name=${encodeURIComponent(name)}`, {
    method: 'DELETE',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ original }),
  });
  throwForStatus(res, MUTATE_STATUS);
  return res.json();
}

export function formatApiError(err) {
  switch (err.message) {
    case 'unauthorized': return 'Session expired';
    case 'offline': return 'Character is not online';
    case 'not_character': return 'Not a character';
    case 'bad_request': return 'Invalid request';
    case 'not_found': return 'Not found';
    case 'desync': return 'State out of sync — please reload';
    case 'player_offline': return 'Player is not online';
    default: return 'Connection error';
  }
}
