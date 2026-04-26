const EMOTE_COLOR = '#CC99FF';

/**
 * Parse a raw history message into speaker name, message text, and flags.
 *
 * Formats handled:
 *   "Name says: Hello"                    → name="Name", isWhisper=false
 *   "Name: Hello"                         → name="Name", isWhisper=false
 *   "Name (privately to Other): Hello"    → name="Name", isWhisper=true
 *   "Name (privately to you): Hello"      → name="Name", isWhisper=true
 *   "Narrator: *something happens*"       → name="Narrator", isNarrator=true
 */
export function parseMessage(raw) {
  const colonIdx = raw.indexOf(':');
  if (colonIdx === -1) return { speaker: null, message: raw, isWhisper: false, isNarrator: false };

  const speakerInfo = raw.substring(0, colonIdx).trim();
  let message = raw.substring(colonIdx + 1).trim();

  let name = speakerInfo;
  let isWhisper = false;
  let isNarrator = false;

  if (speakerInfo === 'Narrator') {
    isNarrator = true;
    // Strip surrounding asterisks from narrator messages
    message = message.replace(/^\*|\*$/g, '');
  } else if (/\(privately to /.test(speakerInfo)) {
    isWhisper = true;
    // Strip "(privately to X)" from the speaker name
    name = speakerInfo.replace(/\s*\(privately to .+?\)\s*$/, '').trim();
  } else {
    name = speakerInfo.replace(/\s+(says|yells|shouts|whispers)$/, '').trim();
  }

  return { name, message, isWhisper, isNarrator };
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
