import { useState, useEffect, useCallback, useRef } from 'preact/hooks';
import { fetchCard, updateCard, setCardPinned, regenerateCard, formatApiError } from './api.js';
import { useToast } from './toast-provider.jsx';

// The six structured persona fields, in render order.
const CARD_FIELDS = [
  { key: 'premise',      label: 'Premise' },
  { key: 'personality',  label: 'Personality' },
  { key: 'values',       label: 'Values' },
  { key: 'background',   label: 'Background' },
  { key: 'speech_style', label: 'Speech Style' },
  { key: 'quirks',       label: 'Quirks' },
];

function provenanceBadge(provenance) {
  if (!provenance) return null;
  const map = {
    generated: { cls: 'bg-secondary',        label: 'Generated' },
    edited:    { cls: 'bg-info text-dark',    label: 'Edited' },
    override:  { cls: 'bg-warning text-dark', label: 'Override' },
  };
  const meta = map[provenance] || { cls: 'bg-secondary', label: provenance };
  return <span class={`badge ${meta.cls}`}>{meta.label}</span>;
}

// Modal for editing a single persona field.
function FieldEditModal({ show, label, value, onSave, onCancel }) {
  const [text, setText] = useState('');
  const inputRef = useRef(null);

  useEffect(() => {
    if (show) {
      setText(value || '');
      requestAnimationFrame(() => { if (inputRef.current) inputRef.current.focus(); });
    }
  }, [show, value]);

  const handleKeyDown = (e) => {
    if (e.key === 'Escape') onCancel();
  };

  if (!show) return null;

  return (
    <div class="modal d-block" tabindex="-1" style="background: rgba(0,0,0,0.5)" onClick={onCancel}>
      <div class="modal-dialog modal-dialog-centered" onClick={(e) => e.stopPropagation()}>
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title">Edit {label}</h5>
            <button type="button" class="btn-close" onClick={onCancel}></button>
          </div>
          <div class="modal-body">
            <textarea
              ref={inputRef}
              class="form-control"
              rows="4"
              value={text}
              onInput={(e) => setText(e.target.value)}
              onKeyDown={handleKeyDown}
              style="resize: vertical"
              placeholder={`Describe the character's ${label.toLowerCase()}…`}
            />
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-primary" onClick={() => onSave(text)}>Save</button>
          </div>
        </div>
      </div>
    </div>
  );
}

// Self-contained character-card panel: rendered preview + structured fields,
// with edit / pin-unpin / regenerate controls for editable (owned, unpinned)
// cards.  Reused by the info sidebar and the browse-all modal.
//
// Props:
//   token, guid    — auth + character
//   open           — when false the panel does not fetch (lazy in accordions)
//   reloadKey      — bump to force a refetch (e.g. on a "card" WS event)
//   canRegen       — show the regenerate control (requires the bot be online)
//   onDesync       — auth/desync escalation
export default function CardSection({ token, guid, open = true, reloadKey = 0, canRegen = false, onDesync }) {
  const [card, setCard] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [busy, setBusy] = useState(false);
  const [editModal, setEditModal] = useState({ show: false, key: null, label: '', value: '' });
  const toast = useToast();

  const guidRef = useRef(guid);
  guidRef.current = guid;

  const load = useCallback(async () => {
    if (!token || !guid) return;
    setLoading(true);
    setError(null);
    try {
      const data = await fetchCard(token, guid);
      if (guidRef.current !== guid) return;
      setCard(data);
    } catch (err) {
      if (guidRef.current !== guid) return;   // a newer character is selected
      if (err.message === 'unauthorized') { onDesync(err.message); return; }
      setError(formatApiError(err));
    } finally {
      if (guidRef.current === guid) setLoading(false);
    }
  }, [token, guid, onDesync]);

  // Reset when the character changes (also closes any open edit modal so a
  // pending save can't post to the newly selected character).
  useEffect(() => {
    setCard(null);
    setError(null);
    setEditModal({ show: false, key: null, label: '', value: '' });
  }, [guid]);

  // Lazy load when the panel is open (and refetch on guid / reloadKey change).
  useEffect(() => { if (open) load(); }, [open, guid, reloadKey]);

  const handleFieldSave = useCallback(async (value) => {
    const key = editModal.key;
    if (!key) return;
    try {
      const updated = await updateCard(token, guid, { [key]: value });
      setCard(updated);
      toast('Card updated', 'success');
      setEditModal({ show: false, key: null, label: '', value: '' });
    } catch (err) {
      if (err.message === 'unauthorized') { onDesync(err.message); return; }
      if (err.message === 'forbidden') { toast('Card is pinned (read-only)', 'error'); return; }
      if (err.message === 'bad_request') { toast('A card needs at least one field', 'error'); return; }
      toast('Failed to update card', 'error');
    }
  }, [token, guid, editModal, toast, onDesync]);

  const handlePinToggle = useCallback(async () => {
    if (!card) return;
    setBusy(true);
    try {
      const res = await setCardPinned(token, guid, !card.pinned);
      const newPinned = !!res.pinned;
      // Pin/unpin only flips the flag — update locally so the UI stays
      // consistent even without a refetch.
      setCard(prev => prev ? { ...prev, pinned: newPinned, editable: !!prev.owned && !newPinned } : prev);
      toast(newPinned ? 'Card pinned' : 'Card unpinned', 'success');
    } catch (err) {
      if (err.message === 'unauthorized') { onDesync(err.message); return; }
      toast('Failed to update pin state', 'error');
    } finally {
      setBusy(false);
    }
  }, [token, guid, card, toast, onDesync]);

  const handleRegen = useCallback(async () => {
    setBusy(true);
    try {
      await regenerateCard(token, guid);
      toast('Card regeneration queued', 'success');
      // The card worker emits a "card" WS event on completion → reloadKey bump.
    } catch (err) {
      if (err.message === 'unauthorized') { onDesync(err.message); return; }
      if (err.message === 'offline') { toast('Character must be online to regenerate', 'error'); return; }
      if (err.message === 'forbidden') { toast('Card is pinned (read-only)', 'error'); return; }
      if (err.message === 'unavailable') { toast('Regeneration unavailable right now', 'error'); return; }
      toast('Failed to queue regeneration', 'error');
    } finally {
      setBusy(false);
    }
  }, [token, guid, toast, onDesync]);

  if (error) {
    return (
      <div class="alert alert-danger py-1 small mb-1" role="alert">
        {error}
        <button class="btn btn-sm btn-link p-0 ms-2" onClick={load}>Retry</button>
      </div>
    );
  }

  // Not-yet-loaded (card === null) shows the spinner too, so switching
  // characters never flashes a stale card or a premature "No data".
  if (loading || !card) {
    return (
      <div class="text-center py-2">
        <div class="spinner-border spinner-border-sm text-primary" role="status">
          <span class="visually-hidden">Loading...</span>
        </div>
      </div>
    );
  }

  const editable = !!card.editable;
  const fields = card.fields || {};

  return (
    <div>
      {/* Badges + controls */}
      <div class="d-flex align-items-center flex-wrap gap-1 mb-2">
        {provenanceBadge(card.provenance)}
        {card.pinned && <span class="badge bg-warning text-dark"><i class="bi bi-pin-angle-fill me-1"></i>Pinned</span>}
        {!card.owned && <span class="badge bg-secondary"><i class="bi bi-eye me-1"></i>Read-only</span>}

        {/* Pin/regenerate only apply once a card row exists; with no row the
            owner authors one by editing a field first. */}
        {card.owned && (card.has_card || card.pinned) && (
          <div class="ms-auto d-flex align-items-center gap-1">
            <button
              class="btn btn-sm btn-outline-secondary py-0 px-1"
              onClick={handlePinToggle}
              disabled={busy}
              title={card.pinned ? 'Unpin (make editable)' : 'Pin (make read-only)'}
            >
              <i class={`bi ${card.pinned ? 'bi-pin-angle-fill' : 'bi-pin-angle'}`}></i>
              <span class="ms-1">{card.pinned ? 'Unpin' : 'Pin'}</span>
            </button>
            {editable && canRegen && (
              <button
                class="btn btn-sm btn-outline-secondary py-0 px-1"
                onClick={handleRegen}
                disabled={busy}
                title="Regenerate this card"
              >
                <i class="bi bi-arrow-clockwise"></i>
                <span class="ms-1">Regenerate</span>
              </button>
            )}
          </div>
        )}
      </div>

      {/* Rendered preview */}
      {card.card && (
        <div class="small text-body-secondary mb-2" style="white-space: pre-wrap; word-break: break-word;">
          {card.card}
        </div>
      )}

      {/* Structured fields (shown when there is a row, or when authoring a new
          card as the owner). */}
      {(card.has_card || editable) && (
        <div class="border-top pt-2">
          {CARD_FIELDS.map(({ key, label }) => {
            const value = fields[key] || '';
            if (!value && !editable) return null;   // hide empty fields when read-only
            return (
              <div key={key} class="mb-2 position-relative">
                <div class="d-flex justify-content-between align-items-baseline">
                  <span class="small fw-bold text-uppercase text-body-secondary" style="font-size: 0.7rem; letter-spacing: 0.03em">{label}</span>
                  {editable && (
                    <button
                      class="btn btn-sm p-0 px-1"
                      title={`Edit ${label}`}
                      onClick={() => setEditModal({ show: true, key, label, value })}
                    >
                      <i class="bi bi-pencil"></i>
                    </button>
                  )}
                </div>
                <div class="small" style="white-space: pre-wrap; word-break: break-word;">
                  {value
                    ? value
                    : <span class="text-body-secondary fst-italic">— empty —</span>}
                </div>
              </div>
            );
          })}
        </div>
      )}

      <FieldEditModal
        show={editModal.show}
        label={editModal.label}
        value={editModal.value}
        onSave={handleFieldSave}
        onCancel={() => setEditModal({ show: false, key: null, label: '', value: '' })}
      />
    </div>
  );
}
