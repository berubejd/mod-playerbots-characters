import { useState, useEffect, useRef, useCallback, useMemo } from 'preact/hooks';
import { fetchHistory, editMessage, deleteMessage, sendWhisper, sendNarrate, sendPartyMessage, sendPartyNarrate, sendTrigger, formatApiError } from './api.js';
import { getMessageMeta, formatMessageParts } from './chat-utils.js';
import { useToast } from './toast-provider.jsx';
import { useMediaQuery } from './use-media-query.js';
import ActionMenu from './action-menu.jsx';
import ChatToolbar from './chat-toolbar.jsx';

// Threshold in px to consider "at bottom" (accounts for fractional pixels)
const SCROLL_BOTTOM_THRESHOLD = 30;
// Threshold in px to auto-scroll on new messages (more lenient than "at bottom")
const AUTO_SCROLL_THRESHOLD = 100;
// Characters per estimated token for context usage bar
const CHARS_PER_TOKEN = 4;

// Build a natural-language "thinking" line from a list of character names.
// "Jon" → "Jon is thinking"
// "Jon, Jane" → "Jon and Jane are thinking"
// "Jon, Jane, Kevin" → "Jon, Jane and Kevin are thinking"
function formatThinkingLine(names) {
  if (names.length === 0) return '';
  if (names.length === 1) return names[0] + ' is thinking';
  if (names.length === 2) return names[0] + ' and ' + names[1] + ' are thinking';
  return names.slice(0, -1).join(', ') + ' and ' + names[names.length - 1] + ' are thinking';
}

function MessageLine({ msg, nameColorMap, onEdit, onDelete, selectionMode, selected, onToggleSelect, isNew }) {
  const meta = typeof msg === 'object' && msg ? getMessageMeta(msg) : { name: 'Narrator', message: '', isWhisper: false, isNarrator: true };
  const id = typeof msg === 'string' ? null : (msg && msg.id);
  const pending = !!(msg && msg.pending);
  const { name, message, isWhisper, isNarrator } = meta;
  const canSelect = id != null && !pending;
  const rawMessage = (msg && msg.message) || '';

  // Narrator messages: horizontal line with centered smaller white text
  if (isNarrator) {
    return (
      <div class={`py-1 message-line position-relative d-flex align-items-center${isNew ? ' message-new' : ''}`} style={`word-break: break-word; text-align: center; margin: 0.5rem 0;${pending ? ' opacity: 0.5;' : ''}`}>
        {selectionMode && canSelect && (
          <input
            type="checkbox"
            class="form-check-input me-2 flex-shrink-0"
            checked={selected}
            onChange={() => onToggleSelect(id)}
          />
        )}
        <div class="flex-grow-1 position-relative">
          <div class="message-actions position-absolute top-0 end-0" style="z-index: 1;">
            {id != null && !pending && (
              <>
                <button
                  class="btn btn-sm p-0 px-1"
                  title="Edit"
                  onClick={(e) => { e.stopPropagation(); onEdit(id, rawMessage); }}
                >
                  <i class="bi bi-pencil"></i>
                </button>
                <button
                  class="btn btn-sm p-0 px-1"
                  title="Delete"
                  onClick={(e) => { e.stopPropagation(); onDelete(id); }}
                >
                  <i class="bi bi-trash3"></i>
                </button>
              </>
            )}
          </div>
          <div style="position: relative;">
            <div style="position: absolute; top: 50%; left: 0; right: 0; border-top: 1px solid #555;"></div>
            <span style="position: relative; display: inline-block; max-width: 75%; color: #fff; font-size: 0.85rem; padding: 0 0.75rem; background: var(--bs-body-bg);">{message}</span>
          </div>
        </div>
      {id != null && !pending && (
        <div class="action-menu position-absolute top-0 end-0">
          <ActionMenu onEdit={() => onEdit(id, rawMessage)} onDelete={() => onDelete(id)} />
        </div>
      )}
      </div>
    );
  }

  const parts = formatMessageParts(message);
  const nameColor = nameColorMap && nameColorMap[name] ? nameColorMap[name] : null;

  return (
    <div class={`py-1 message-line position-relative d-flex${isNew ? ' message-new' : ''}`} style={`word-break: break-word;${pending ? ' opacity: 0.5;' : ''}`}>
      {selectionMode && canSelect && (
        <input
          type="checkbox"
          class="form-check-input me-2 flex-shrink-0 mt-1"
          checked={selected}
          onChange={() => onToggleSelect(id)}
        />
      )}
      <div class="flex-grow-1 position-relative">
        <div class="message-actions position-absolute top-0 end-0" style="z-index: 1;">
          {id != null && !pending && (
            <>
              <button
                class="btn btn-sm p-0 px-1"
                title="Edit"
                onClick={(e) => { e.stopPropagation(); onEdit(id, rawMessage); }}
              >
                <i class="bi bi-pencil"></i>
              </button>
              <button
                class="btn btn-sm p-0 px-1"
                title="Delete"
                onClick={(e) => { e.stopPropagation(); onDelete(id); }}
              >
                <i class="bi bi-trash3"></i>
              </button>
            </>
          )}
        </div>
        {name && (
          <span class="fw-bold" style={nameColor ? `color: ${nameColor}` : undefined}>{name}</span>
        )}
        {isWhisper && (
          <span class="badge bg-secondary ms-1" style="font-size: 0.65rem; vertical-align: middle;">WHISPER</span>
        )}
        {name && ': '}
        {parts.map((part, j) => {
          if (part.type === 'emote') {
            return (
              <span key={j} style={`color: ${part.color}`} class="fst-italic">
                *{part.text}*
              </span>
            );
          }
          return <span key={j} style="white-space: pre-wrap">{part.text}</span>;
        })}
      </div>
      {id != null && !pending && (
        <div class="action-menu position-absolute top-0 end-0">
          <ActionMenu onEdit={() => onEdit(id, rawMessage)} onDelete={() => onDelete(id)} />
        </div>
      )}
    </div>
  );
}

function EditModal({ show, text, onSave, onCancel }) {
  const [value, setValue] = useState(text);
  const inputRef = useRef(null);

  useEffect(() => {
    if (show) {
      setValue(text);
      requestAnimationFrame(() => {
        if (inputRef.current) inputRef.current.focus();
      });
    }
  }, [show, text]);

  const handleKeyDown = (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      if (!e.shiftKey) {
        onSave(value);
      }
    } else if (e.key === 'Escape') {
      onCancel();
    }
  };

  if (!show) return null;

  return (
    <div class="modal d-block" tabindex="-1" style="background: rgba(0,0,0,0.5)" onClick={onCancel}>
      <div class="modal-dialog modal-dialog-centered" onClick={(e) => e.stopPropagation()}>
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title">Edit Message</h5>
            <button type="button" class="btn-close" onClick={onCancel}></button>
          </div>
          <div class="modal-body">
            <textarea
              ref={inputRef}
              class="form-control"
              rows="5"
              value={value}
              onInput={(e) => {
                const v = e.target.value.replace(/\n/g, '');
                setValue(v);
              }}
              onKeyDown={handleKeyDown}
              style="resize: vertical"
            />
            <div class="alert alert-info small mt-3 mb-0">
              This message may be shared across multiple characters' histories. Editing it will affect <strong>all</strong> of them.
            </div>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-primary" onClick={() => onSave(value)}>Save</button>
          </div>
        </div>
      </div>
    </div>
  );
}

function DeleteModal({ show, onConfirm, onCancel }) {
  if (!show) return null;

  return (
    <div class="modal d-block" tabindex="-1" style="background: rgba(0,0,0,0.5)" onClick={onCancel}>
      <div class="modal-dialog modal-dialog-centered" onClick={(e) => e.stopPropagation()}>
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title">Delete Message</h5>
            <button type="button" class="btn-close" onClick={onCancel}></button>
          </div>
          <div class="modal-body">
            <p class="mb-0">Deleting messages is irreversible, are you sure you want to delete this one?</p>
            <div class="alert alert-info small mt-3 mb-0">
              This message will be permanently deleted from <strong>all</strong> characters' histories.
            </div>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-danger" onClick={() => onConfirm()}>Delete</button>
          </div>
        </div>
      </div>
    </div>
  );
}

function BatchDeleteModal({ show, count, onConfirm, onCancel }) {
  if (!show) return null;

  return (
    <div class="modal d-block" tabindex="-1" style="background: rgba(0,0,0,0.5)" onClick={onCancel}>
      <div class="modal-dialog modal-dialog-centered" onClick={(e) => e.stopPropagation()}>
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title">Delete Messages</h5>
            <button type="button" class="btn-close" onClick={onCancel}></button>
          </div>
          <div class="modal-body">
            <p class="mb-0">Deleting messages is irreversible. Are you sure you want to delete {count} message{count !== 1 ? 's' : ''}?</p>
            <div class="alert alert-info small mt-3 mb-0">
              Selected messages will be permanently deleted from <strong>all</strong> characters' histories.
            </div>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-danger" onClick={() => onConfirm()}>Delete</button>
          </div>
        </div>
      </div>
    </div>
  );
}

const MODE_OPTIONS = [
  { value: 'whisper', label: 'Whisper (/w)', shortcut: '/w', placeholder: 'Send a whisper…' },
  { value: 'narrate', label: 'Narrate (/n)', shortcut: '/n', placeholder: 'Send a narration…' },
  { value: 'party', label: 'Party (/p)', shortcut: '/p', placeholder: 'Send a party message…' },
  { value: 'narrate-party', label: 'Narrate Party (/np)', shortcut: '/np', placeholder: 'Send a party narration…' },
  { value: 'trigger', label: 'Trigger (/tr)', shortcut: '/tr', placeholder: 'Enter character name to trigger…' },
];

// Filter available modes based on character category and whether it's the player's own character.
//   'party'  — character is online and in the player's party: all modes
//   'online' — character is online but not in the party: no /p or /np
//   'offline' — character is offline: no modes (input disabled)
// Additionally, whisper is never available when viewing the player's own character.
function getAvailableModes(playerGuid, selectedGuid, charCategory) {
  if (charCategory === 'offline') return [];

  let modes;
  if (charCategory === 'party') {
    modes = MODE_OPTIONS;
  } else {
    // online — exclude party and narrate-party modes
    modes = MODE_OPTIONS.filter(m => m.value !== 'party' && m.value !== 'narrate-party');
  }

  // Filter out whisper when viewing the player's own character
  if (playerGuid && selectedGuid === playerGuid) {
    modes = modes.filter(m => m.value !== 'whisper');
  }

  return modes;
}

// Ordered longest-shortcut-first so '/np' is tried before '/n'
const MODE_SHORTCUTS = [...MODE_OPTIONS].sort((a, b) => b.shortcut.length - a.shortcut.length);

function SendMessageInput({ token, selectedGuid, playerGuid, onDesync, onMessageSent, messageMode, onMessageModeChange, characters, charCategory }) {
  const [text, setText] = useState('');
  const [sending, setSending] = useState(false);
  const textareaRef = useRef(null);
  const toast = useToast();
  const isMobile = useMediaQuery('(max-width: 767px)');

  const availableModes = getAvailableModes(playerGuid, selectedGuid, charCategory);
  const modeConfig = availableModes.find((m) => m.value === messageMode) || availableModes[0] || null;

  const adjustHeight = useCallback(() => {
    const el = textareaRef.current;
    if (!el) return;
    el.style.height = 'auto';
    const lineHeight = parseFloat(getComputedStyle(el).lineHeight) || 24;
    const maxHeight = lineHeight * 3 + 8;
    el.style.height = Math.min(el.scrollHeight, maxHeight) + 'px';
  }, []);

  const handleSend = useCallback(async () => {
    const trimmed = text.trim();
    if (!trimmed || sending || !modeConfig) return;

    // Bare slash command (e.g. just "/p") — switch mode and clear input
    const bareMatch = MODE_SHORTCUTS.find((opt) => trimmed.toLowerCase() === opt.shortcut);
    if (bareMatch) {
      if (availableModes.some(m => m.value === bareMatch.value)) {
        onMessageModeChange(bareMatch.value);
      }
      setText('');
      if (textareaRef.current) textareaRef.current.style.height = 'auto';
      return;
    }

    setSending(true);
    try {
      switch (messageMode) {
        case 'narrate':
          await sendNarrate(token, selectedGuid, trimmed);
          break;
        case 'party':
          await sendPartyMessage(token, trimmed);
          break;
        case 'narrate-party':
          await sendPartyNarrate(token, trimmed);
          break;
        case 'trigger': {
          const char = (characters || []).find((c) => c.name.toLowerCase() === trimmed.toLowerCase());
          if (!char) {
            toast(`Character "${trimmed}" not found`, 'error');
            setSending(false);
            requestAnimationFrame(() => { if (textareaRef.current) textareaRef.current.focus(); });
            return;
          }
          await sendTrigger(token, char.guid);
          toast(`Triggered ${char.name}`, 'success');
          break;
        }
        default:
          await sendWhisper(token, selectedGuid, trimmed);
          break;
      }
      setText('');
      if (textareaRef.current) {
        textareaRef.current.style.height = 'auto';
      }
      if (onMessageSent && messageMode !== 'trigger') onMessageSent(trimmed, messageMode);
    } catch (err) {
      if (err.message === 'unauthorized') {
        onDesync(err.message);
        return;
      }
      toast(`Failed to send ${modeConfig.label.toLowerCase()}`, 'error');
    } finally {
      setSending(false);
      requestAnimationFrame(() => {
        if (textareaRef.current) textareaRef.current.focus();
      });
    }
  }, [text, sending, token, selectedGuid, toast, onDesync, onMessageSent, messageMode, modeConfig, onMessageModeChange, characters, availableModes]);

  const handleKeyDown = (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  // Offline character — disable input entirely
  if (charCategory === 'offline') {
    return (
      <div class="d-flex align-items-end gap-2 p-3 border-top bg-body">
        <div class="form-control text-body-secondary text-center" style="min-height: 38px; display: flex; align-items: center; justify-content: center;" disabled>
          Character is offline
        </div>
      </div>
    );
  }

  return (
    <div class="d-flex align-items-end gap-2 p-3 border-top bg-body">
      <select
        class="form-select flex-shrink-0"
        style={isMobile ? 'width: auto; min-height: 38px; padding-left: 0.4rem; padding-right: 1.4rem; font-size: 0.85rem;' : 'width: auto; min-height: 38px;'}
        value={messageMode}
        onChange={(e) => onMessageModeChange(e.target.value)}
        disabled={sending}
      >
        {availableModes.map((opt) => (
          <option key={opt.value} value={opt.value}>{isMobile ? opt.shortcut : opt.label}</option>
        ))}
      </select>
      <textarea
        ref={textareaRef}
        class="form-control"
        rows="1"
        placeholder={modeConfig ? modeConfig.placeholder : ''}
        value={text}
        onInput={(e) => {
          let v = e.target.value.replace(/\n/g, '');
          const lower = v.toLowerCase();
          const matched = MODE_SHORTCUTS.find((opt) => lower.startsWith(opt.shortcut + ' '));
          if (matched) {
            if (matched.value !== messageMode && availableModes.some(m => m.value === matched.value)) {
              onMessageModeChange(matched.value);
            }
            v = v.slice(matched.shortcut.length + 1);
          }
          setText(v);
          adjustHeight();
        }}
        onKeyDown={handleKeyDown}
        style="resize: none; min-height: 38px;"
        disabled={sending}
      />
      <button
        class="btn btn-primary flex-shrink-0"
        onClick={handleSend}
        disabled={sending || !text.trim()}
        title="Send"
      >
        <i class="bi bi-send"></i>
      </button>
    </div>
  );
}

// Module-level ref — survives ChatView remounts caused by key={selectedGuid} in player-view.jsx
const lastManualModeRef = { current: 'whisper' };

export default function ChatView({ token, selectedGuid, playerGuid, nameColorMap, charName, thinkingNames = [], playerName, chatEvent, chatReloadKey, onDesync, characters, messageMode, onMessageModeChange, maxHistoryCtx, charCategory }) {
  const [messages, setMessages] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [retryKey, setRetryKey] = useState(0);
  const [showScrollDown, setShowScrollDown] = useState(false);
  const toast = useToast();
  const isDesktop = useMediaQuery('(min-width: 768px)');

  // Edit modal state
  const [editModal, setEditModal] = useState({ show: false, id: null, text: '' });
  // Delete modal state
  const [deleteModal, setDeleteModal] = useState({ show: false, id: null });

  // Selection mode state (desktop only)
  const [selectionMode, setSelectionMode] = useState(false);
  const [selectedIds, setSelectedIds] = useState(new Set());
  const [batchDeleteModal, setBatchDeleteModal] = useState({ show: false, count: 0 });

  // Track newly arrived message IDs for highlight animation
  const [newMessageIds, setNewMessageIds] = useState(new Set());
  const newTimersRef = useRef(new Map());

  const containerRef = useRef(null);
  const pendingWhisperRef = useRef(null);
  const loadingRef = useRef(false);
  const isAtBottomRef = useRef(true);
  const shouldAutoScrollRef = useRef(true);
  const prevScrollTopRef = useRef(0);

  // Track input overlay height so we can add matching spacer in the scroll area
  const [inputHeight, setInputHeight] = useState(0);
  const inputWrapperRef = useRef(null);

  // Check if the scroll container is at the bottom
  const checkIsAtBottom = useCallback(() => {
    const el = containerRef.current;
    if (!el) return true;
    return el.scrollHeight - el.scrollTop - el.clientHeight < SCROLL_BOTTOM_THRESHOLD;
  }, []);

  // Scroll to bottom helper
  const scrollToBottom = useCallback(() => {
    const el = containerRef.current;
    if (!el) return;
    el.scrollTop = el.scrollHeight;
    prevScrollTopRef.current = el.scrollTop;
    isAtBottomRef.current = true;
    shouldAutoScrollRef.current = true;
    setShowScrollDown(false);
  }, []);

  // Check if the scroll container is near the bottom (for auto-scroll decisions)
  const checkIsNearBottom = useCallback(() => {
    const el = containerRef.current;
    if (!el) return true;
    return el.scrollHeight - el.scrollTop - el.clientHeight < AUTO_SCROLL_THRESHOLD;
  }, []);

  // Handle scroll events
  const handleScroll = useCallback(() => {
    const el = containerRef.current;
    if (!el) return;
    const atBottom = checkIsAtBottom();
    isAtBottomRef.current = atBottom;
    const scrollTop = el.scrollTop;

    if (scrollTop < prevScrollTopRef.current - 2) {
      shouldAutoScrollRef.current = checkIsNearBottom();
    } else if (atBottom) {
      shouldAutoScrollRef.current = true;
    }

    prevScrollTopRef.current = scrollTop;
    setShowScrollDown(!atBottom);
  }, [checkIsAtBottom, checkIsNearBottom]);

  // Add a pending message to the chat after successful send.
  // Narrate/narrate-party are immediate (no LLM call) — the WS history event
  // arrives fast enough that an optimistic pending entry just causes a race.
  const handleMessageSent = useCallback((message, mode) => {
    if (mode === 'narrate' || mode === 'narrate-party') return;

    // Store the raw message text for matching against the WS event
    pendingWhisperRef.current = message;

    const text = mode === 'party'
      ? `You: ${message}`
      : `You (privately): ${message}`;

    setMessages((prev) => {
      const withoutPending = prev ? prev.filter((m) => m.id !== 'pending') : [];
      return [...withoutPending, {
        id: 'pending',
        text,
        type: mode === 'whisper' ? 7 : 2,
        author_guid: playerGuid,
        author_name: playerName,
        message,
        pending: true,
      }];
    });
  }, [playerName, playerGuid]);

  // Fetch all history when selectedGuid changes, retry is clicked, or chatReloadKey changes
  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    loadingRef.current = true;
    setError(null);
    setMessages(null);
    pendingWhisperRef.current = null;
    shouldAutoScrollRef.current = true;
    setSelectionMode(false);
    setSelectedIds(new Set());

    // Clear highlight timers on character change
    for (const t of newTimersRef.current.values()) clearTimeout(t);
    newTimersRef.current.clear();
    setNewMessageIds(new Set());

    fetchHistory(token, selectedGuid)
      .then((data) => {
        if (!cancelled) {
          setMessages(data.messages);
          setLoading(false);
          loadingRef.current = false;
        }
      })
      .catch((err) => {
        if (!cancelled) {
          if (err.message === 'unauthorized') {
            onDesync(err.message);
            return;
          }
          setLoading(false);
          loadingRef.current = false;
          setError(formatApiError(err));
        }
      });

    return () => { cancelled = true; };
  }, [selectedGuid, token, retryKey, chatReloadKey]);

  // Process WS events (history and thinks)
  useEffect(() => {
    if (!chatEvent) return;

    if (chatEvent.type === 'thinks') {
      return;
    }

    if (chatEvent.type === 'history') {

      if (loadingRef.current) return;

      const msg = chatEvent.data.message;
      const { id } = msg;
      const pendingRaw = pendingWhisperRef.current;

      // Helper: mark a message as new (highlight) and auto-remove after animation
      const markAsNew = (msgId) => {
        setNewMessageIds((prev) => new Set(prev).add(msgId));
        if (newTimersRef.current.has(msgId)) clearTimeout(newTimersRef.current.get(msgId));
        newTimersRef.current.set(msgId, setTimeout(() => {
          setNewMessageIds((prev) => {
            const next = new Set(prev);
            next.delete(msgId);
            return next;
          });
          newTimersRef.current.delete(msgId);
        }, 3000));
      };

      // Ignore preview/temporary messages (id=0) — only process real messages
      if (id === 0) return;

      if (pendingRaw !== null) {
        if (msg.message === pendingRaw) {
          // Exact match — replace pending with real (removes opacity)
          pendingWhisperRef.current = null;
          setMessages((prev) => prev.map((m) => m.id === 'pending' ? msg : m));
          markAsNew(id);
        } else if (msg.type === 0) {
          // Narrator line (e.g. "some time passes") — insert BEFORE the
          // pending placeholder. The time-gap represents a passage of
          // time that occurred before the player's message was processed,
          // so server-side ordering has it first.
          setMessages((prev) => {
            const pendingIdx = prev.findIndex(m => m.id === 'pending');
            if (pendingIdx !== -1) {
              const newArr = [...prev];
              newArr.splice(pendingIdx, 0, msg);
              return newArr;
            }
            return [...prev, msg];
          });
          markAsNew(id);
        } else {
          // Character response — insert it right after the pending
          // placeholder so the player's own message stays first
          // (the player spoke before characters responded).
          setMessages((prev) => {
            const pendingIdx = prev.findIndex(m => m.id === 'pending');
            if (pendingIdx !== -1) {
              const newArr = [...prev];
              newArr.splice(pendingIdx + 1, 0, msg);
              return newArr;
            }
            return [...prev, msg];
          });
          markAsNew(id);
        }
      } else {
        // No pending — append with dedup by real DB id
        setMessages((prev) => {
          if (!prev) return [msg];
          if (prev.some(m => m.id === id)) return prev;
          return [...prev, msg];
        });
        markAsNew(id);
      }
    }
  }, [chatEvent]);

  // Scroll to bottom after messages change
  useEffect(() => {
    if (!messages || !containerRef.current) return;

    if (shouldAutoScrollRef.current) {
      const el = containerRef.current;
      el.scrollTop = el.scrollHeight;
      prevScrollTopRef.current = el.scrollTop;
      isAtBottomRef.current = true;
      shouldAutoScrollRef.current = true;
      setShowScrollDown(false);
    }
  }, [messages]);

  // ResizeObserver for chat container
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;

    const observer = new ResizeObserver(() => {
      if (shouldAutoScrollRef.current) {
        el.scrollTop = el.scrollHeight;
        prevScrollTopRef.current = el.scrollTop;
      }
    });

    observer.observe(el);
    return () => observer.disconnect();
  }, [loading]);

  // Observe input wrapper height for spacer
  useEffect(() => {
    const el = inputWrapperRef.current;
    if (!el) return;

    const observer = new ResizeObserver(() => {
      setInputHeight(el.offsetHeight);
    });

    observer.observe(el);
    return () => observer.disconnect();
  }, [messages]);

  // Cleanup highlight timers on unmount
  useEffect(() => {
    return () => {
      for (const t of newTimersRef.current.values()) clearTimeout(t);
    };
  }, []);

  // Wrapper that remembers the last mode the user explicitly chose.
  // This is passed to SendMessageInput for manual changes (dropdown, slash commands).
  // The auto-switch effect below calls onMessageModeChange directly to avoid
  // overwriting the remembered preference.
  const handleManualModeChange = useCallback((newMode) => {
    lastManualModeRef.current = newMode;
    onMessageModeChange(newMode);
  }, [onMessageModeChange]);

  // Auto-switch from whisper/invalid mode when viewing player's own character
  // or when the current mode is not available for the character category.
  // When the user's last manual choice becomes available again (e.g. switching
  // back from the player character to a normal character), restore it.
  useEffect(() => {
    const available = getAvailableModes(playerGuid, selectedGuid, charCategory);
    if (available.length === 0) return; // offline — no modes to switch to

    const isCurrentAvailable = available.some(m => m.value === messageMode);

    if (!isCurrentAvailable) {
      // Current mode not available for this character — auto-switch.
      // Prefer the user's last manual choice if it fits, otherwise fall back to the first available.
      if (lastManualModeRef.current && available.some(m => m.value === lastManualModeRef.current)) {
        onMessageModeChange(lastManualModeRef.current);
      } else {
        onMessageModeChange(available[0].value);
      }
    } else if (
      lastManualModeRef.current !== messageMode &&
      available.some(m => m.value === lastManualModeRef.current)
    ) {
      // The user's last manual choice is available again (e.g. switching back
      // from the player character where whisper was removed) — restore it.
      onMessageModeChange(lastManualModeRef.current);
    }
  }, [playerGuid, selectedGuid, charCategory, messageMode, onMessageModeChange]);

  // When the input overlay height changes, scroll to the new bottom if user was at bottom
  useEffect(() => {
    if (!containerRef.current || !inputHeight) return;
    if (shouldAutoScrollRef.current) {
      const el = containerRef.current;
      el.scrollTop = el.scrollHeight;
      prevScrollTopRef.current = el.scrollTop;
    }
  }, [inputHeight]);

  const handleRetry = useCallback(() => {
    setRetryKey((k) => k + 1);
  }, []);

  // Selection mode handlers (desktop only)
  const toggleSelectionMode = useCallback(() => {
    setSelectionMode((prev) => !prev);
    setSelectedIds(new Set());
  }, []);

  const toggleMessageSelection = useCallback((id) => {
    setSelectedIds((prev) => {
      const next = new Set(prev);
      if (next.has(id)) {
        next.delete(id);
      } else {
        next.add(id);
      }
      return next;
    });
  }, []);

  // Edit handlers
  const handleEditOpen = useCallback((id, text) => {
    setEditModal({ show: true, id, text });
  }, []);

  const handleEditSave = useCallback(async (newText) => {
    const messageId = editModal.id;
    setSelectionMode(false);
    setSelectedIds(new Set());
    try {
      await editMessage(token, selectedGuid, messageId, newText);
      setMessages((prev) =>
        prev.map((msg) => {
          if (msg.id !== messageId) return msg;
          // Regenerate pre-rendered text from raw message + type/author_name
          let renderedText;
          if (msg.type === 0) {
            renderedText = `Narrator: *${newText}*`;
          } else if (msg.type === 7) {
            renderedText = `${msg.author_name || 'Unknown'} (privately to you): ${newText}`;
          } else {
            renderedText = `${msg.author_name || 'Unknown'}: ${newText}`;
          }
          return { ...msg, message: newText, text: renderedText };
        })
      );
      setEditModal({ show: false, id: null, text: '' });
      toast('Message updated', 'success');
    } catch (err) {
      if (err.message === 'desync' || err.message === 'unauthorized') {
        onDesync(err.message);
        return;
      }
      toast('Failed to update message', 'error');
    }
  }, [token, selectedGuid, editModal.id, toast, onDesync]);

  const handleEditCancel = useCallback(() => {
    setEditModal({ show: false, id: null, text: '' });
  }, []);

  // Delete handlers
  const handleDeleteOpen = useCallback((id) => {
    setDeleteModal({ show: true, id });
  }, []);

  const handleDeleteConfirm = useCallback(async () => {
    const messageId = deleteModal.id;
    setSelectionMode(false);
    setSelectedIds(new Set());
    try {
      await deleteMessage(token, selectedGuid, messageId);
      setMessages((prev) => prev.filter((msg) => msg.id !== messageId));
      setDeleteModal({ show: false, id: null });
      toast('Message deleted', 'success');
    } catch (err) {
      if (err.message === 'desync' || err.message === 'unauthorized') {
        onDesync(err.message);
        return;
      }
      toast('Failed to delete message', 'error');
    }
  }, [token, selectedGuid, deleteModal.id, toast, onDesync]);

  const handleDeleteCancel = useCallback(() => {
    setDeleteModal({ show: false, id: null });
  }, []);

  // Batch delete handlers
  const handleBatchDeleteOpen = useCallback(() => {
    setBatchDeleteModal({ show: true, count: selectedIds.size });
  }, [selectedIds]);

  const handleBatchDeleteCancel = useCallback(() => {
    setBatchDeleteModal({ show: false, count: 0 });
  }, []);

  const handleBatchDeleteConfirm = useCallback(async () => {
    const toDelete = messages
      .filter((msg) => selectedIds.has(msg.id) && msg.id != null && !msg.pending)
      .map((msg) => ({ id: msg.id }))
      .sort((a, b) => b.id - a.id);

    setSelectionMode(false);
    setSelectedIds(new Set());
    setBatchDeleteModal({ show: false, count: 0 });

    if (toDelete.length === 0) return;

    let deletedCount = 0;
    for (const msg of toDelete) {
      try {
        await deleteMessage(token, selectedGuid, msg.id);
        setMessages((prev) => prev.filter((m) => m.id !== msg.id));
        deletedCount++;
      } catch (err) {
        if (err.message === 'desync' || err.message === 'unauthorized') {
          onDesync(err.message);
          return;
        }
        toast('Failed to delete some messages', 'error');
        break;
      }
    }

    if (deletedCount > 0) {
      toast(`${deletedCount} message${deletedCount !== 1 ? 's' : ''} deleted`, 'success');
    }
  }, [token, selectedGuid, messages, selectedIds, toast, onDesync]);

  // Render: loading
  if (loading) {
    return (
      <div class="d-flex justify-content-center align-items-center h-100 flex-grow-1">
        <div class="text-center">
          <div class="spinner-border text-primary mb-3" role="status">
            <span class="visually-hidden">Loading...</span>
          </div>
          <p class="text-body-secondary">Loading chat history…</p>
        </div>
      </div>
    );
  }

  // Render: error
  if (error) {
    return (
      <div class="d-flex justify-content-center align-items-center h-100 flex-grow-1">
        <div class="text-center">
          <div class="alert alert-danger" role="alert">{error}</div>
          <button class="btn btn-primary" onClick={handleRetry}>Retry</button>
        </div>
      </div>
    );
  }

  // Estimate token usage from current messages
  const estimatedTokens = useMemo(() => {
    if (!messages) return 0;
    let totalChars = 0;
    for (const msg of messages) {
      totalChars += (typeof msg === 'string' ? msg : msg.text).length;
    }
    return Math.ceil(totalChars / CHARS_PER_TOKEN);
  }, [messages]);

  // Context fill percentage (0–100), clamped
  const contextPercent = maxHistoryCtx > 0
    ? Math.min((estimatedTokens / maxHistoryCtx) * 100, 100)
    : 0;

  // Progress bar color based on fill level
  const contextBarColor = contextPercent < 40
    ? '#198754'
    : contextPercent < 80
      ? '#ffc107'
      : '#dc3545';

  // Render: empty history
  if (messages && messages.length === 0) {
    return (
      <div class="d-flex flex-column h-100">
        <div class="d-flex justify-content-center align-items-center flex-grow-1 position-relative">
          <p class="text-body-secondary">No messages yet</p>
          {thinkingNames.length > 0 && (
            <div class="position-absolute bottom-0 start-0 px-3 pb-1" style="font-size: 0.8rem; color: var(--bs-secondary-color);">
              {formatThinkingLine(thinkingNames)}…
            </div>
          )}
        </div>
        {maxHistoryCtx > 0 && (
          <div style="height: 3px; background: var(--bs-tertiary-bg, #2c2c2c);">
            <div style={`height: 100%; width: ${contextPercent}%; background: ${contextBarColor}; transition: width 0.3s ease, background-color 0.3s ease;`}></div>
          </div>
        )}
        <SendMessageInput token={token} selectedGuid={selectedGuid} playerGuid={playerGuid} onDesync={onDesync} onMessageSent={handleMessageSent} messageMode={messageMode} onMessageModeChange={handleManualModeChange} characters={characters} charCategory={charCategory} />
      </div>
    );
  }

  // Render: messages
  return (
    <div class="d-flex flex-column h-100 position-relative">
      {messages && messages.length > 0 && (
        <ChatToolbar
          token={token}
          selectedGuid={selectedGuid}
          charName={charName}
          isDesktop={isDesktop}
          selectionMode={selectionMode}
          selectedIds={selectedIds}
          onToggleSelectionMode={toggleSelectionMode}
          onBatchDeleteOpen={handleBatchDeleteOpen}
          onDesync={onDesync}
        />
      )}
      <div class="position-relative flex-grow-1" style="min-height: 0">
        <div ref={containerRef} class={`overflow-auto p-3${selectionMode ? ' selection-mode' : ''}`} style={`height: calc(100% - ${inputHeight}px); font-size: 1.25rem`} onScroll={handleScroll}>
          {messages.map((msg) => (
            <MessageLine
              key={msg.id}
              msg={msg}
              nameColorMap={nameColorMap}
              onEdit={handleEditOpen}
              onDelete={handleDeleteOpen}
              selectionMode={selectionMode}
              selected={selectedIds.has(msg.id)}
              onToggleSelect={toggleMessageSelection}
              isNew={newMessageIds.has(msg.id)}
            />
          ))}
        </div>
        {thinkingNames.length > 0 && (
          <div class="position-absolute start-0 px-3 pb-1" style={`font-size: 0.8rem; color: var(--bs-secondary-color); bottom: ${inputHeight}px;`}>
            {formatThinkingLine(thinkingNames)}…
          </div>
        )}
        {showScrollDown && (
          <button
            class="btn btn-secondary position-absolute"
            style={`bottom: ${inputHeight + 18}px; right: 18px; border-radius: 50%; width: 36px; height: 36px; padding: 0; font-size: 1.3rem; line-height: 36px; text-align: center; z-index: 5;`}
            onClick={scrollToBottom}
            title="Scroll to bottom"
          >
            <i class="bi bi-arrow-down-circle"></i>
          </button>
        )}
      </div>
      <div ref={inputWrapperRef} class="position-absolute bottom-0 start-0 end-0" style="z-index: 10;">
        {maxHistoryCtx > 0 && (
          <div style="height: 3px; background: var(--bs-tertiary-bg, #2c2c2c);">
            <div style={`height: 100%; width: ${contextPercent}%; background: ${contextBarColor}; transition: width 0.3s ease, background-color 0.3s ease;`}></div>
          </div>
        )}
        <SendMessageInput token={token} selectedGuid={selectedGuid} playerGuid={playerGuid} onDesync={onDesync} onMessageSent={handleMessageSent} messageMode={messageMode} onMessageModeChange={handleManualModeChange} characters={characters} charCategory={charCategory} />
      </div>
      <EditModal
        show={editModal.show}
        text={editModal.text}
        onSave={handleEditSave}
        onCancel={handleEditCancel}
      />
      <DeleteModal
        show={deleteModal.show}
        onConfirm={handleDeleteConfirm}
        onCancel={handleDeleteCancel}
      />
      <BatchDeleteModal
        show={batchDeleteModal.show}
        count={batchDeleteModal.count}
        onConfirm={handleBatchDeleteConfirm}
        onCancel={handleBatchDeleteCancel}
      />
    </div>
  );
}
