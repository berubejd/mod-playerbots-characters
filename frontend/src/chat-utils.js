const EMOTE_COLOR = '#CC99FF';

/**
 * Extract structured metadata from a message object returned by the API.
 * No string parsing needed — the API provides type, author_name, and message fields.
 */
export function getMessageMeta(msg) {
  if (msg.type === 0) {
    return { name: 'Narrator', message: msg.message, isWhisper: false, isNarrator: true };
  }
  return {
    name: msg.author_name || 'Unknown',
    message: msg.message,
    isWhisper: msg.type === 7,
    isNarrator: false,
  };
}

/**
 * Format a message string into an array of renderable parts,
 * handling *emote* blocks only.
 *
 * Each part is { type: 'text'|'emote', text, color? }
 */
export function formatMessageParts(text) {
  const parts = [];

  // Find emote blocks: *text*
  const emoteRegex = /\*([^*]+)\*/g;
  let match;
  let lastIndex = 0;

  while ((match = emoteRegex.exec(text)) !== null) {
    if (match.index > lastIndex) {
      parts.push({ type: 'text', text: text.substring(lastIndex, match.index) });
    }
    parts.push({ type: 'emote', text: match[1], color: EMOTE_COLOR });
    lastIndex = match.index + match[0].length;
  }

  if (lastIndex < text.length) {
    parts.push({ type: 'text', text: text.substring(lastIndex) });
  }

  return parts;
}
