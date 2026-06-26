import { useState, useEffect, useCallback } from 'preact/hooks';
import { sendTrigger, fetchDebugRequest, formatApiError } from './api.js';
import { useToast } from './toast-provider.jsx';

// --- Debug Request modal ---
// (Extracted from chat-toolbar.jsx so it can be opened for any online character,
// not just the currently selected one.)

function DebugRequestModal({ show, data, loading, error, charName, onClose }) {
  const [openSection, setOpenSection] = useState('general');

  useEffect(() => {
    if (show) setOpenSection('general');
  }, [show]);

  if (!show) return null;

  const LONG_THRESHOLD = 200;

  const renderField = (label, value) => {
    if (value == null) return null;
    const str = String(value);
    const isLong = str.length > LONG_THRESHOLD;
    return (
      <div class="mb-3">
        <label class="form-label fw-semibold small text-uppercase text-body-secondary mb-1">{label}</label>
        {isLong ? (
          <textarea
            class="form-control form-control-sm font-monospace"
            rows="8"
            value={str}
            readOnly
            style="resize: vertical; font-size: 0.8rem;"
          />
        ) : (
          <div class="form-control-plaintext font-monospace" style="font-size: 0.85rem;">{str}</div>
        )}
      </div>
    );
  };

  const renderTokenStat = (label, used, limit) => {
    if (used == null || limit == null) return null;
    const pct = limit > 0 ? Math.min((used / limit) * 100, 100) : 0;
    const color = pct < 40 ? '#198754' : pct < 80 ? '#ffc107' : '#dc3545';
    return (
      <div class="mb-2">
        <div class="d-flex justify-content-between small">
          <span class="text-body-secondary">{label}</span>
          <span class="font-monospace">{used.toLocaleString()} / {limit.toLocaleString()}</span>
        </div>
        <div style="height: 4px; background: var(--bs-tertiary-bg, #2c2c2c); border-radius: 2px;">
          <div style={`height: 100%; width: ${pct}%; background: ${color}; border-radius: 2px; transition: width 0.3s ease;`}></div>
        </div>
      </div>
    );
  };

  const toggleSection = (section) => {
    setOpenSection((prev) => prev === section ? null : section);
  };

  const renderSection = (id, title, content) => (
    <div class="accordion-item">
      <h2 class="accordion-header">
        <button
          class={`accordion-button${openSection === id ? '' : ' collapsed'}`}
          type="button"
          onClick={() => toggleSection(id)}
        >
          {title}
        </button>
      </h2>
      <div class={`accordion-collapse collapse${openSection === id ? ' show' : ''}`}>
        <div class="accordion-body">
          {content}
        </div>
      </div>
    </div>
  );

  return (
    <div class="modal d-block" tabindex="-1" style="background: rgba(0,0,0,0.5)" onClick={onClose}>
      <div class="modal-dialog modal-xl modal-dialog-centered modal-dialog-scrollable" onClick={(e) => e.stopPropagation()}>
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title"><i class="bi bi-eyeglasses me-2"></i>Character Debug — {charName}</h5>
            <button type="button" class="btn-close" onClick={onClose}></button>
          </div>
          <div class="modal-body">
            {loading && (
              <div class="d-flex justify-content-center align-items-center py-4">
                <div class="spinner-border text-primary me-2" role="status"></div>
                <span class="text-body-secondary">Building request…</span>
              </div>
            )}
            {error && (
              <div class="alert alert-danger" role="alert">{error}</div>
            )}
            {data && !loading && (
              <div class="accordion">
                {renderSection('general', 'General Info', (
                  <>
                    {renderTokenStat('History Tokens', data.history_tokens, data.history_token_limit)}
                    {renderTokenStat('Memory Tokens', data.memory_tokens, data.memory_token_limit)}
                    <div class="mt-2 small">
                      <span class="text-body-secondary">Condensation: <span class={data.condensation_needed ? 'text-warning' : 'text-success'}>{data.condensation_needed ? 'Needed' : 'Not needed'}</span></span>
                    </div>
                  </>
                ))}

                {renderSection('prompts', 'Prompts', (
                  <>
                    {renderField('System Prompt', data.system_prompt)}
                    {renderField('User Prompt', data.user_prompt)}
                  </>
                ))}

                {data.snapshot && renderSection('snapshot', 'Snapshot', (
                  <>
                    {renderField('Character Card', data.snapshot.character_card)}
                    {renderField('Context', data.snapshot.context)}
                    {renderField('Scene', data.snapshot.scene)}
                    {renderField('Combat Status', data.snapshot.combat_status)}
                    {renderField('Equipment', data.snapshot.equipment)}
                    {renderField('Group', data.snapshot.char_group)}
                    {renderField('Line of Sight', data.snapshot.char_los)}
                    {renderField('Memories', data.snapshot.memories)}
                    {renderField('Relationships', data.snapshot.relationships)}
                    {renderField('Chat History', data.snapshot.chat_history)}
                  </>
                ))}
              </div>
            )}
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onClose}>Close</button>
          </div>
        </div>
      </div>
    </div>
  );
}

/**
 * Per-character quick-action toolbar rendered to the right of a character card
 * in the character list. Only shown for online characters.
 *
 * Contains (top to bottom):
 *   - Trigger  (POST /api/char/:guid/trigger)
 *   - Debug    (GET  /api/char/:guid/debug/request)
 *
 * Each button operates on the character it belongs to, independent of the
 * currently selected character.
 */
export default function CharacterActions({ token, guid, charName, onDesync }) {
  const toast = useToast();
  const [triggering, setTriggering] = useState(false);
  const [debugModal, setDebugModal] = useState({ show: false, data: null, loading: false, error: null });

  const handleTrigger = useCallback(async () => {
    if (triggering) return;
    setTriggering(true);
    try {
      await sendTrigger(token, guid);
      toast(`Triggered ${charName || 'character'}`, 'success');
    } catch (err) {
      if (err.message === 'unauthorized') {
        onDesync(err.message);
        return;
      }
      toast(`Failed to trigger ${charName || 'character'}`, 'error');
    } finally {
      setTriggering(false);
    }
  }, [token, guid, charName, triggering, toast, onDesync]);

  const handleDebugRequest = useCallback(async () => {
    setDebugModal({ show: true, data: null, loading: true, error: null });
    try {
      const data = await fetchDebugRequest(token, guid);
      setDebugModal({ show: true, data, loading: false, error: null });
    } catch (err) {
      if (err.message === 'unauthorized') {
        onDesync(err.message);
        return;
      }
      setDebugModal({ show: true, data: null, loading: false, error: formatApiError(err) });
    }
  }, [token, guid, onDesync]);

  const handleDebugClose = useCallback(() => {
    setDebugModal({ show: false, data: null, loading: false, error: null });
  }, []);

  return (
    <>
      <div
        class="d-flex flex-column align-items-center justify-content-between card flex-shrink-0"
        style="width: 30px; padding: 4px 0;"
        title="Quick actions"
      >
        <button
          type="button"
          class="btn btn-link p-0 lh-1 text-body"
          onClick={handleTrigger}
          disabled={triggering}
          title="Trigger"
        >
          {triggering
            ? <span class="spinner-border spinner-border-sm"></span>
            : <i class="bi bi-lightning-charge"></i>}
        </button>
        <hr class="w-100 my-1" style="margin-left: 0; margin-right: 0;" />
        <button
          type="button"
          class="btn btn-link p-0 lh-1 text-body"
          onClick={handleDebugRequest}
          title="Character Debug"
        >
          <i class="bi bi-eyeglasses"></i>
        </button>
      </div>

      <DebugRequestModal
        show={debugModal.show}
        data={debugModal.data}
        loading={debugModal.loading}
        error={debugModal.error}
        charName={charName}
        onClose={handleDebugClose}
      />
    </>
  );
}
