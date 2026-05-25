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
const AUTH_STATUS = { 401: 'unauthorized' };
const CHAR_STATUS = { ...AUTH_STATUS, 404: 'not_found', 403: 'forbidden' };
const CHAR_ONLINE_STATUS = { ...AUTH_STATUS, 404: 'offline', 403: 'forbidden' };
const MUTATE_STATUS = { ...AUTH_STATUS, 400: 'bad_request', 404: 'not_found', 409: 'desync' };

export async function exchangeOtp(otp) {
  const res = await fetch(`/api/token?otp=${encodeURIComponent(otp)}`);
  if (res.status === 401) throw new Error('invalid');
  if (!res.ok) throw new Error('error');
  const data = await res.json();
  return data.token;
}

export async function fetchAccount(token) {
  const res = await fetch('/api/account', {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, AUTH_STATUS);
  return res.json();
}

export async function fetchCard(token, guid) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/card`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_STATUS);
  return res.json();
}

export async function fetchCharData(token, guid) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/data`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_STATUS);
  return res.json();
}

export async function updateCharData(token, guid, data) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/data`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(data),
  });
  throwForStatus(res, MUTATE_STATUS);
  return res.json();
}

export async function fetchMemoryCount(token, guid) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/memory/count`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_STATUS);
  return res.json();
}

export async function fetchMemories(token, guid, { order_by = 'id', order_dir = 'desc', page = 1, limit = 10 } = {}) {
  const params = new URLSearchParams();
  if (order_by) params.set('order_by', order_by);
  if (order_dir) params.set('order_dir', order_dir);
  if (page) params.set('page', page);
  if (limit) params.set('limit', limit);
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/memory?${params}`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_STATUS);
  return res.json();
}

export async function editMemory(token, guid, id, memory_text, importance, original) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/memory/${encodeURIComponent(id)}`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ memory_text, importance, original }),
  });
  throwForStatus(res, MUTATE_STATUS);
  return res.json();
}

export async function deleteMemory(token, guid, id, original) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/memory/${encodeURIComponent(id)}`, {
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
  throwForStatus(res, CHAR_ONLINE_STATUS);
  return res.json();
}

export async function fetchParty(token) {
  const res = await fetch('/api/party', {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, AUTH_STATUS);
  return res.json();
}

export async function fetchConfig(token) {
  const res = await fetch('/api/config', {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, AUTH_STATUS);
  return res.json();
}

export async function fetchHistory(token, guid) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/history`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_STATUS);
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
  throwForStatus(res, { ...CHAR_ONLINE_STATUS, 400: 'bad_request' });
  return res.json();
}

export async function sendTrigger(token, guid) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/trigger`, {
    method: 'POST',
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_ONLINE_STATUS);
  return res.json();
}

export async function sendNarrate(token, guid, message) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/narrate`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ message }),
  });
  throwForStatus(res, { ...CHAR_STATUS, 400: 'bad_request' });
  return res.json();
}

export async function sendPartyMessage(token, message) {
  const res = await fetch('/api/party/message', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ message }),
  });
  throwForStatus(res, { ...AUTH_STATUS, 400: 'bad_request', 404: 'not_found' });
  return res.json();
}

export async function sendPartyNarrate(token, message) {
  const res = await fetch('/api/party/narrate', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ message }),
  });
  throwForStatus(res, { ...AUTH_STATUS, 400: 'bad_request', 404: 'not_found' });
  return res.json();
}

export async function editMessage(token, guid, id, message) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/history?id=${encodeURIComponent(id)}`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ message }),
  });
  throwForStatus(res, MUTATE_STATUS);
  return res.json();
}

export async function deleteMessage(token, guid, id) {
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/history?id=${encodeURIComponent(id)}`, {
    method: 'DELETE',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({}),
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

export async function fetchDebugRequest(token, guid, event = '') {
  const params = new URLSearchParams();
  if (event) params.set('event', event);
  const res = await fetch(`/api/char/${encodeURIComponent(guid)}/debug/request?${params}`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  throwForStatus(res, CHAR_ONLINE_STATUS);
  return res.json();
}

export function formatApiError(err) {
  switch (err.message) {
    case 'unauthorized': return 'Session expired';
    case 'forbidden': return 'Access denied';
    case 'offline': return 'Character is not online';
    case 'not_character': return 'Not a character';
    case 'bad_request': return 'Invalid request';
    case 'not_found': return 'Not found';
    case 'desync': return 'State out of sync — please reload';
    default: return 'Connection error';
  }
}
