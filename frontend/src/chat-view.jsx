import { useState, useEffect, useRef, useCallback } from 'preact/hooks';
import { fetchHistory, editMessage, deleteMessage, sendWhisper, sendNarrate, sendPartyMessage, sendPartyNarrate, formatApiError } from './api.js';
import { parseMessage, formatMessageParts } from './chat-utils.js';
import { useToast } from './toast-provider.jsx';
import { useMediaQuery } from './use-media-query.js';
import ActionMenu from './action-menu.jsx';

// Threshold in px to consider "at bottom" (accounts for fractional pixels)
const SCROLL_BOTTOM_THRESHOLD = 30;
// Threshold in px to auto-scroll on new messages (more lenient than "at bottom")
const AUTO_SCROLL_THRESHOLD = 100;

function MessageLine({ msg, nameColorMap, onEdit, onDelete, selectionMode, selected, onToggleSelect }) {
  const text = typeof msg === 'string' ? msg : msg.text;
  const id = typeof msg === 'string' ? null : msg.id;
  const pending = !!(msg && msg.pending);
  const { name, message, isWhisper, isNarrator } = parseMessage(text);
  const canSelect = id != null && !pending;

  // Narrator messages: horizontal line with centered smaller white text
  if (isNarrator) {
    return (
      <div class="py-1 message-line position-relative d-flex align-items-center" style={`word-break: break-word; text-align: center; margin: 0.5rem 0;${pending ? ' opacity: 0.5;' : ''}`}>
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
                  onClick={(e) => { e.stopPropagation(); onEdit(id, text); }}
                >
                  <i class="bi bi-pencil"></i>
                </button>
                <button
                  class="btn btn-sm p-0 px-1"
                  title="Delete"
                  onClick={(e) => { e.stopPropagation(); onDelete(id, text); }}
                >
                  <i class="bi bi-trash3"></i>
                </button>
              </>
            )}
          </div>
          {id != null && !pending && (
            <div class="action-menu position-absolute top-0 end-0">
              <ActionMenu onEdit={() => onEdit(id, text)} onDelete={() => onDelete(id, text)} />
            </div>
          )}
          <div style="position: relative;">
            <div style="position: absolute; top: 50%; left: 0; right: 0; border-top: 1px solid #555;"></div>
            <span style="position: relative; display: inline-block; max-width: 75%; color: #fff; font-size: 0.85rem; padding: 0 0.75rem; background: var(--bs-body-bg);">{message}</span>
          </div>
        </div>
      </div>
    );
  }

  const parts = formatMessageParts(message);
  const nameColor = nameColorMap && nameColorMap[name] ? nameColorMap[name] : null;

  return (
    <div class="py-1 message-line position-relative d-flex" style={`word-break: break-word;${pending ? ' opacity: 0.5;' : ''}`}>
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
                onClick={(e) => { e.stopPropagation(); onEdit(id, text); }}
              >
                <i class="bi bi-pencil"></i>
              </button>
              <button
                class="btn btn-sm p-0 px-1"
                title="Delete"
                onClick={(e) => { e.stopPropagation(); onDelete(id, text); }}
              >
                <i class="bi bi-trash3"></i>
              </button>
            </>
          )}
        </div>
        {id != null && !pending && (
          <div class="action-menu position-absolute top-0 end-0">
            <ActionMenu onEdit={() => onEdit(id, text)} onDelete={() => onDelete(id, text)} />
          </div>
        )}
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
    </div>
  );
}

function EditModal({ show, text, onSave, onCancel }) {
  const [value, setValue] = useState(text);
  const [propagate, setPropagate] = useState(false);
  const inputRef = useRef(null);

  useEffect(() => {
    if (show) {
      setValue(text);
      setPropagate(false);
      // Focus input after modal opens
      requestAnimationFrame(() => {
        if (inputRef.current) inputRef.current.focus();
      });
    }
  }, [show, text]);

  const handleKeyDown = (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      if (!e.shiftKey) {
        onSave(value, propagate);
      }
      // Shift+Enter is prevented but doesn't save — no newlines allowed
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
                // Prevent newlines
                const v = e.target.value.replace(/\n/g, '');
                setValue(v);
              }}
              onKeyDown={handleKeyDown}
              style="resize: vertical"
            />
            <div class="form-check mt-3">
              <input
                class="form-check-input"
                type="checkbox"
                id="editPropagate"
                checked={propagate}
                onChange={(e) => setPropagate(e.target.checked)}
              />
              <label class="form-check-label small" for="editPropagate">
                Find and edit the same message in other group members' histories
              </label>
            </div>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-primary" onClick={() => onSave(value, propagate)}>Save</button>
          </div>
        </div>
      </div>
    </div>
  );
}

function DeleteModal({ show, onConfirm, onCancel }) {
  const [propagate, setPropagate] = useState(false);

  useEffect(() => {
    if (show) {
      setPropagate(false);
    }
  }, [show]);

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
            <div class="form-check mt-3">
              <input
                class="form-check-input"
                type="checkbox"
                id="deletePropagate"
                checked={propagate}
                onChange={(e) => setPropagate(e.target.checked)}
              />
              <label class="form-check-label small" for="deletePropagate">
                Find and delete the same message in other group members' histories
              </label>
            </div>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-danger" onClick={() => onConfirm(propagate)}>Delete</button>
          </div>
        </div>
      </div>
    </div>
  );
}

function BatchDeleteModal({ show, count, onConfirm, onCancel }) {
  const [propagate, setPropagate] = useState(false);

  useEffect(() => {
    if (show) {
      setPropagate(false);
    }
  }, [show]);

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
            <div class="form-check mt-3">
              <input
                class="form-check-input"
                type="checkbox"
                id="batchDeletePropagate"
                checked={propagate}
                onChange={(e) => setPropagate(e.target.checked)}
              />
              <label class="form-check-label small" for="batchDeletePropagate">
                Find and delete the same messages in other group members' histories
              </label>
            </div>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-danger" onClick={() => onConfirm(propagate)}>Delete</button>
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
];

// Ordered longest-shortcut-first so '/np' is tried before '/n'
const MODE_SHORTCUTS = [...MODE_OPTIONS].sort((a, b) => b.shortcut.length - a.shortcut.length);

function SendMessageInput({ token, selectedGuid, onDesync, onMessageSent, messageMode, onMessageModeChange }) {
  const [text, setText] = useState('');
  const [sending, setSending] = useState(false);
  const textareaRef = useRef(null);
  const toast = useToast();

  const modeConfig = MODE_OPTIONS.find((m) => m.value === messageMode) || MODE_OPTIONS[0];

  const adjustHeight = useCallback(() => {
    const el = textareaRef.current;
    if (!el) return;
    el.style.height = 'auto';
    // Max height = ~3 lines (1 line ≈ 1.5em + padding)
    const lineHeight = parseFloat(getComputedStyle(el).lineHeight) || 24;
    const maxHeight = lineHeight * 3 + 8; // 3 lines + small padding
    el.style.height = Math.min(el.scrollHeight, maxHeight) + 'px';
  }, []);

  const handleSend = useCallback(async () => {
    const trimmed = text.trim();
    if (!trimmed || sending) return;

    // Bare slash command (e.g. just "/p") — switch mode and clear input
    const bareMatch = MODE_SHORTCUTS.find((opt) => trimmed.toLowerCase() === opt.shortcut);
    if (bareMatch) {
      onMessageModeChange(bareMatch.value);
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
        default:
          await sendWhisper(token, selectedGuid, trimmed);
          break;
      }
      setText('');
      if (textareaRef.current) {
        textareaRef.current.style.height = 'auto';
      }
      // Show pending message in chat — WS events will handle confirmation
      if (onMessageSent) onMessageSent(trimmed, messageMode);
    } catch (err) {
      if (err.message === 'player_offline') {
        onDesync(err.message);
        return;
      }
      toast(`Failed to send ${modeConfig.label.toLowerCase()}`, 'error');
    } finally {
      setSending(false);
      // Refocus the textarea after re-enable (state update needs a frame to flush)
      requestAnimationFrame(() => {
        if (textareaRef.current) textareaRef.current.focus();
      });
    }
  }, [text, sending, token, selectedGuid, toast, onDesync, onMessageSent, messageMode, modeConfig, onMessageModeChange]);

  const handleKeyDown = (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  return (
    <div class="d-flex align-items-end gap-2 p-3 border-top bg-body">
      <select
        class="form-select flex-shrink-0"
        style="width: auto; min-height: 38px;"
        value={messageMode}
        onChange={(e) => onMessageModeChange(e.target.value)}
        disabled={sending}
      >
        {MODE_OPTIONS.map((opt) => (
          <option key={opt.value} value={opt.value}>{opt.label}</option>
        ))}
      </select>
      <textarea
        ref={textareaRef}
        class="form-control"
        rows="1"
        placeholder={modeConfig.placeholder}
        value={text}
        onInput={(e) => {
          // Prevent newlines
          let v = e.target.value.replace(/\n/g, '');
          // Check for slash-command mode switching at the start of input
          // e.g. "/w hello" → switch to whisper, keep "hello"
          const lower = v.toLowerCase();
          const matched = MODE_SHORTCUTS.find((opt) => lower.startsWith(opt.shortcut + ' '));
          if (matched && matched.value !== messageMode) {
            onMessageModeChange(matched.value);
            v = v.slice(matched.shortcut.length + 1); // strip shortcut + space
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

export default function ChatView({ token, selectedGuid, nameColorMap, charName, playerName, chatEvent, chatReloadKey, onLoadComplete, onDesync, characters, messageMode, onMessageModeChange }) {
  const [messages, setMessages] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [retryKey, setRetryKey] = useState(0);
  const [thinking, setThinking] = useState(false);
  const [showScrollDown, setShowScrollDown] = useState(false);
  const toast = useToast();
  const isDesktop = useMediaQuery('(min-width: 768px)');

  // Edit modal state
  const [editModal, setEditModal] = useState({ show: false, id: null, text: '' });
  // Delete modal state (includes original text for desync detection)
  const [deleteModal, setDeleteModal] = useState({ show: false, id: null, text: '' });

  // Selection mode state (desktop only)
  const [selectionMode, setSelectionMode] = useState(false);
  const [selectedIds, setSelectedIds] = useState(new Set());
  const [batchDeleteModal, setBatchDeleteModal] = useState({ show: false, count: 0 });

  const containerRef = useRef(null);
  const lastIdRef = useRef(0);
  const pendingWhisperRef = useRef(null); // pending whisper text, or null
  const loadingRef = useRef(false);
  const isAtBottomRef = useRef(true);
  // Whether the next messages change should auto-scroll to bottom
  const shouldAutoScrollRef = useRef(true);
  // Previous scrollTop — used to detect user scrolling UP vs content being added
  const prevScrollTopRef = useRef(0);

  // Track input overlay height so we can add matching spacer in the scroll area
  const [inputHeight, setInputHeight] = useState(0);
  const inputWrapperRef = useRef(null);

  // Use ref for callback to avoid it being a dependency of the fetch effect
  const onLoadCompleteRef = useRef(onLoadComplete);
  onLoadCompleteRef.current = onLoadComplete;

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

  // Handle scroll events — track whether user is at bottom.
  // Key insight: only disable auto-scroll when the user actively scrolls UP.
  // When new content is added at the bottom, scrollHeight increases but scrollTop
  // stays the same — this must NOT disable auto-scrolling, otherwise rapid
  // messages cause the chat to stop scrolling to the bottom.
  const handleScroll = useCallback(() => {
    const el = containerRef.current;
    if (!el) return;
    const atBottom = checkIsAtBottom();
    isAtBottomRef.current = atBottom;
    const scrollTop = el.scrollTop;

    if (scrollTop < prevScrollTopRef.current - 2) {
      // User scrolled up — update auto-scroll intent based on proximity to bottom
      shouldAutoScrollRef.current = checkIsNearBottom();
    } else if (atBottom) {
      // Reached the bottom (by scrolling down or programmatically) — enable auto-scroll
      shouldAutoScrollRef.current = true;
    }
    // Otherwise: new content was added or user scrolled down but not to bottom.
    // Keep the current auto-scroll decision unchanged.

    prevScrollTopRef.current = scrollTop;
    setShowScrollDown(!atBottom);
  }, [checkIsAtBottom, checkIsNearBottom]);

  // Add a pending message to the chat after successful send
  const handleMessageSent = useCallback((message, mode) => {
    let fullText;
    switch (mode) {
      case 'narrate':
      case 'narrate-party':
        fullText = `Narrator: *${message}*`;
        break;
      case 'party':
        fullText = `${playerName} says: ${message}`;
        break;
      default:
        fullText = `${playerName} (privately to you): ${message}`;
        break;
    }
    pendingWhisperRef.current = fullText;
    setMessages((prev) => {
      const withoutPending = prev ? prev.filter((m) => m.id !== 'pending') : [];
      return [...withoutPending, { id: 'pending', text: fullText, pending: true }];
    });
  }, [playerName]);

  // Fetch all history when selectedGuid changes, retry is clicked, or chatReloadKey changes
  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    loadingRef.current = true;
    setError(null);
    setMessages(null);
    setThinking(false);
    lastIdRef.current = 0;
    pendingWhisperRef.current = null;
    // Always scroll to bottom after a full reload
    shouldAutoScrollRef.current = true;
    // Reset selection mode on character change
    setSelectionMode(false);
    setSelectedIds(new Set());

    fetchHistory(token, selectedGuid)
      .then((data) => {
        if (!cancelled) {
          setMessages(data.messages);
          if (data.messages.length > 0) {
            lastIdRef.current = data.messages[data.messages.length - 1].id;
          }
          setLoading(false);
          loadingRef.current = false;
          if (onLoadCompleteRef.current) onLoadCompleteRef.current();
        }
      })
      .catch((err) => {
        if (!cancelled) {
          if (err.message === 'player_offline') {
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
      setThinking(true);
      return;
    }

    if (chatEvent.type === 'history') {
      setThinking(false);

      // Skip processing while loading — the fetch will include this message
      if (loadingRef.current) return;

      const { id, text } = chatEvent.data.message;
      const expectedId = lastIdRef.current + 1;
      const pendingText = pendingWhisperRef.current;

      if (pendingText !== null) {
        pendingWhisperRef.current = null;
        if (text === pendingText) {
          // Same message confirmed — replace pending with real (removes opacity)
          setMessages((prev) => prev.map((m) => m.id === 'pending' ? { id, text } : m));
          lastIdRef.current = id;
        } else {
          // Different message — remove pending, add the real one
          setMessages((prev) => {
            const withoutPending = prev.filter((m) => m.id !== 'pending');
            if (id === expectedId) {
              return [...withoutPending, { id, text }];
            }
            return withoutPending;
          });
          if (id === expectedId) {
            lastIdRef.current = id;
          } else {
            setRetryKey((k) => k + 1);
          }
        }
      } else {
        if (id === expectedId) {
          // Append message to the end of the chat.
          // Auto-scroll decision is tracked by handleScroll — don't re-evaluate
          // here; the messages effect will scroll synchronously if appropriate.
          setMessages((prev) => prev ? [...prev, { id, text }] : prev);
          lastIdRef.current = id;
        } else {
          // ID mismatch — something went wrong, invalidate and re-fetch
          setRetryKey((k) => k + 1);
        }
      }
    }
  }, [chatEvent]);

  // Scroll to bottom after messages change (if appropriate).
  // Scroll synchronously — useEffect runs after paint so scrollHeight is
  // already correct. Using requestAnimationFrame introduced a race condition:
  // when multiple messages arrive rapidly, a scroll event from a previous
  // programmatic scroll could fire between the RAF scheduling and execution,
  // causing handleScroll to disable auto-scroll prematurely.
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

  // When the chat container resizes (e.g. virtual keyboard opens/closes),
  // scroll to the new bottom if the user was at the bottom.
  // Uses ResizeObserver which fires after the layout has been recalculated,
  // unlike the visualViewport resize which fires before CSS updates.
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

  // Observe input wrapper height so we can keep a matching spacer in the scroll area
  useEffect(() => {
    const el = inputWrapperRef.current;
    if (!el) return;

    const observer = new ResizeObserver(() => {
      setInputHeight(el.offsetHeight);
    });

    observer.observe(el);
    return () => observer.disconnect();
  }, [messages]);

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

  const handleEditSave = useCallback(async (newText, propagate) => {
    const originalText = editModal.text;
    const messageId = editModal.id;
    // Exit selection mode to avoid stale ID references
    setSelectionMode(false);
    setSelectedIds(new Set());
    try {
      await editMessage(token, selectedGuid, messageId, newText, originalText);
      // Update the message in local state
      setMessages((prev) =>
        prev.map((msg) =>
          msg.id === messageId ? { ...msg, text: newText } : msg
        )
      );
      setEditModal({ show: false, id: null, text: '' });

      let count = 1;
      if (propagate) {
        const otherChars = (characters || []).filter((c) => c.guid !== selectedGuid);
        const results = await Promise.allSettled(
          otherChars.map(async (char) => {
            const data = await fetchHistory(token, char.guid);
            // Search from latest to oldest for the same original message
            const msgs = data.messages;
            for (let i = msgs.length - 1; i >= 0; i--) {
              if (msgs[i].text === originalText) {
                await editMessage(token, char.guid, msgs[i].id, newText, originalText);
                return true;
              }
            }
            return false;
          })
        );
        count += results.filter((r) => r.status === 'fulfilled' && r.value).length;
      }

      toast(propagate ? `${count} message${count !== 1 ? 's' : ''} updated` : 'Message updated', 'success');
    } catch (err) {
      if (err.message === 'desync' || err.message === 'player_offline') {
        onDesync(err.message);
        return;
      }
      toast('Failed to update message', 'error');
    }
  }, [token, selectedGuid, editModal.id, editModal.text, toast, onDesync, characters]);

  const handleEditCancel = useCallback(() => {
    setEditModal({ show: false, id: null, text: '' });
  }, []);

  // Delete handlers
  const handleDeleteOpen = useCallback((id, text) => {
    setDeleteModal({ show: true, id, text });
  }, []);

  const handleDeleteConfirm = useCallback(async (propagate) => {
    const originalText = deleteModal.text;
    const messageId = deleteModal.id;
    // Exit selection mode to avoid stale ID references
    setSelectionMode(false);
    setSelectedIds(new Set());
    try {
      await deleteMessage(token, selectedGuid, messageId, originalText);
      // Remove the message from local state and re-index remaining messages.
      // The backend uses 1-based array indices as IDs, so after an element is
      // erased all subsequent items shift down by one position.
      const deletedId = messageId;
      setMessages((prev) =>
        prev
          .filter((msg) => msg.id !== deletedId)
          .map((msg) => (msg.id > deletedId ? { ...msg, id: msg.id - 1 } : msg))
      );
      lastIdRef.current -= 1;
      setDeleteModal({ show: false, id: null, text: '' });

      let count = 1;
      if (propagate) {
        const otherChars = (characters || []).filter((c) => c.guid !== selectedGuid);
        const results = await Promise.allSettled(
          otherChars.map(async (char) => {
            const data = await fetchHistory(token, char.guid);
            // Search from latest to oldest for the same original message
            const msgs = data.messages;
            for (let i = msgs.length - 1; i >= 0; i--) {
              if (msgs[i].text === originalText) {
                await deleteMessage(token, char.guid, msgs[i].id, originalText);
                return true;
              }
            }
            return false;
          })
        );
        count += results.filter((r) => r.status === 'fulfilled' && r.value).length;
      }

      toast(propagate ? `${count} message${count !== 1 ? 's' : ''} deleted` : 'Message deleted', 'success');
    } catch (err) {
      if (err.message === 'desync' || err.message === 'player_offline') {
        onDesync(err.message);
        return;
      }
      toast('Failed to delete message', 'error');
    }
  }, [token, selectedGuid, deleteModal.id, deleteModal.text, toast, onDesync, characters]);

  const handleDeleteCancel = useCallback(() => {
    setDeleteModal({ show: false, id: null, text: '' });
  }, []);

  // Batch delete handlers
  const handleBatchDeleteOpen = useCallback(() => {
    setBatchDeleteModal({ show: true, count: selectedIds.size });
  }, [selectedIds]);

  const handleBatchDeleteCancel = useCallback(() => {
    setBatchDeleteModal({ show: false, count: 0 });
  }, []);

  const handleBatchDeleteConfirm = useCallback(async (propagate) => {
    // Capture selected messages before clearing state
    const toDelete = messages
      .filter((msg) => selectedIds.has(msg.id) && msg.id != null && !msg.pending)
      .map((msg) => ({ id: msg.id, text: msg.text }))
      .sort((a, b) => b.id - a.id); // Sort descending — delete highest IDs first

    // Exit selection mode immediately
    setSelectionMode(false);
    setSelectedIds(new Set());
    setBatchDeleteModal({ show: false, count: 0 });

    if (toDelete.length === 0) return;

    const deletedTexts = [];
    for (const msg of toDelete) {
      try {
        await deleteMessage(token, selectedGuid, msg.id, msg.text);
        deletedTexts.push(msg.text);
        // Update local state: remove the message and re-index subsequent ones
        setMessages((prev) => {
          const deletedId = msg.id;
          return prev
            .filter((m) => m.id !== deletedId)
            .map((m) => (m.id > deletedId ? { ...m, id: m.id - 1 } : m));
        });
        lastIdRef.current -= 1;
      } catch (err) {
        if (err.message === 'desync' || err.message === 'player_offline') {
          onDesync(err.message);
          return;
        }
        toast('Failed to delete some messages', 'error');
        break;
      }
    }

    if (deletedTexts.length === 0) return;

    // Handle propagation
    if (propagate) {
      const otherChars = (characters || []).filter((c) => c.guid !== selectedGuid);
      let propagateCount = 0;
      for (const char of otherChars) {
        try {
          const data = await fetchHistory(token, char.guid);
          // Find all matching messages, sort by ID descending for safe deletion
          const matchingMsgs = data.messages
            .filter((m) => deletedTexts.includes(m.text))
            .sort((a, b) => b.id - a.id);
          for (const matchMsg of matchingMsgs) {
            try {
              await deleteMessage(token, char.guid, matchMsg.id, matchMsg.text);
              propagateCount++;
            } catch {
              // Skip failed propagations for individual messages
            }
          }
        } catch {
          // Skip failed character history fetches
        }
      }
      const totalCount = deletedTexts.length + propagateCount;
      toast(`${totalCount} message${totalCount !== 1 ? 's' : ''} deleted`, 'success');
    } else {
      toast(`${deletedTexts.length} message${deletedTexts.length !== 1 ? 's' : ''} deleted`, 'success');
    }
  }, [token, selectedGuid, messages, selectedIds, toast, onDesync, characters]);

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

  // Render: empty history
  if (messages && messages.length === 0) {
    return (
      <div class="d-flex flex-column h-100">
        <div class="d-flex justify-content-center align-items-center flex-grow-1 position-relative">
          <p class="text-body-secondary">No messages yet</p>
          {thinking && charName && (
            <div class="position-absolute bottom-0 start-0 px-3 pb-1" style="font-size: 0.8rem; color: var(--bs-secondary-color);">
              {charName} thinks…
            </div>
          )}
        </div>
        <SendMessageInput token={token} selectedGuid={selectedGuid} onDesync={onDesync} onMessageSent={handleMessageSent} messageMode={messageMode} onMessageModeChange={onMessageModeChange} />
      </div>
    );
  }

  // Render: messages
  return (
    <div class="d-flex flex-column h-100 position-relative">
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
            />
          ))}
        </div>
        {isDesktop && messages && messages.length > 0 && (
          <div class="chat-toolbar position-absolute top-0 start-0 m-2" style="z-index: 5;">
            <button
              class="btn btn-sm p-0 px-1"
              style={selectionMode ? 'color: var(--bs-primary) !important;' : undefined}
              onClick={toggleSelectionMode}
              title={selectionMode ? 'Cancel selection' : 'Select messages'}
            >
              <i class="bi bi-check-square"></i>
            </button>
            {selectionMode && selectedIds.size > 0 && (
              <button
                class="btn btn-sm p-0 px-1"
                style="color: var(--bs-danger) !important;"
                onClick={handleBatchDeleteOpen}
                title="Delete selected"
              >
                <i class="bi bi-trash"></i>
              </button>
            )}
          </div>
        )}
        {thinking && charName && (
          <div class="position-absolute start-0 px-3 pb-1" style={`font-size: 0.8rem; color: var(--bs-secondary-color); bottom: ${inputHeight}px;`}>
            {charName} thinks…
          </div>
        )}
        {showScrollDown && (
          <button
            class="btn btn-secondary position-absolute"
            style={`bottom: ${inputHeight + 18}px; right: 18px; border-radius: 50%; width: 36px; height: 36px; padding: 0; font-size: 1.3rem; line-height: 36px; text-align: center;`}
            onClick={scrollToBottom}
            title="Scroll to bottom"
          >
            <i class="bi bi-arrow-down-circle"></i>
          </button>
        )}
      </div>
      <div ref={inputWrapperRef} class="position-absolute bottom-0 start-0 end-0" style="z-index: 10;">
        <SendMessageInput token={token} selectedGuid={selectedGuid} onDesync={onDesync} onMessageSent={handleMessageSent} messageMode={messageMode} onMessageModeChange={onMessageModeChange} />
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
