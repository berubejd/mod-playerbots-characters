import { useState, useEffect, useCallback, useMemo } from 'preact/hooks';
import { fetchCards, formatApiError } from './api.js';
import { getClassColor } from './wow-colors.js';
import CardSection from './card-section.jsx';

const genderIcon = {
  Male: 'bi-gender-male',
  Female: 'bi-gender-female',
};

function provLabel(provenance) {
  const map = { generated: 'Generated', edited: 'Edited', override: 'Override' };
  return map[provenance] || provenance;
}

// Browse every character that has a card row (across all accounts). Read-only
// for characters you don't own; owned cards remain editable/pinnable in place.
export default function BrowseModal({ token, show, wsEvent, accountChars = [], onClose, onDesync }) {
  const [cards, setCards] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [selected, setSelected] = useState(null);
  const [filter, setFilter] = useState('');
  const [detailReloadKey, setDetailReloadKey] = useState(0);

  const load = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const data = await fetchCards(token);
      setCards(data.cards || []);
    } catch (err) {
      if (err.message === 'unauthorized') { onDesync(err.message); return; }
      setError(formatApiError(err));
    } finally {
      setLoading(false);
    }
  }, [token, onDesync]);

  // Silent refresh (no spinner / error takeover) for WS-driven list updates, so
  // list badges stay in sync with detail-pane edits without flicker.
  const refreshList = useCallback(async () => {
    try {
      const data = await fetchCards(token);
      setCards(data.cards || []);
    } catch (err) {
      if (err.message === 'unauthorized') onDesync(err.message);
    }
  }, [token, onDesync]);

  useEffect(() => {
    if (show) {
      load();
      setSelected(null);
      setFilter('');
    }
  }, [show]);

  // On a "card" event (edit/pin/regenerate), refresh the list badges, and bump
  // the detail pane when it concerns the selected character.
  useEffect(() => {
    if (!show || !wsEvent || wsEvent.type !== 'card') return;
    if (selected && wsEvent.data && wsEvent.data.guid === selected) {
      setDetailReloadKey(k => k + 1);
    }
    refreshList();
  }, [wsEvent, show, selected, refreshList]);

  const filtered = useMemo(() => {
    const list = cards || [];
    const f = filter.trim().toLowerCase();
    if (!f) return list;
    return list.filter(c => (c.name || '').toLowerCase().includes(f));
  }, [cards, filter]);

  const selectedEntry = useMemo(
    () => (cards || []).find(c => c.guid === selected) || null,
    [cards, selected]
  );

  // Fresh online status for owned characters comes from the account data (kept
  // up to date via online/offline events in app.jsx), not the one-shot card
  // list — so the Regenerate gate doesn't go stale while the modal is open.
  const ownedOnline = useMemo(() => {
    const m = {};
    for (const c of accountChars) m[c.guid] = !!c.is_online;
    return m;
  }, [accountChars]);

  const canRegenSelected = !!(selectedEntry && selectedEntry.owned && ownedOnline[selected]);

  if (!show) return null;

  return (
    <div class="modal d-block" tabindex="-1" style="background: rgba(0,0,0,0.5)" onClick={onClose}>
      <div class="modal-dialog modal-xl modal-dialog-centered modal-dialog-scrollable" onClick={(e) => e.stopPropagation()}>
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title"><i class="bi bi-collection me-2"></i>Encountered Characters</h5>
            <button type="button" class="btn-close" onClick={onClose}></button>
          </div>
          <div class="modal-body">
            <div class="row g-3">
              {/* List */}
              <div class="col-12 col-md-5 border-end-md">
                <input
                  type="text"
                  class="form-control form-control-sm mb-2"
                  placeholder="Filter by name…"
                  value={filter}
                  onInput={(e) => setFilter(e.target.value)}
                />
                {loading ? (
                  <div class="text-center py-3">
                    <div class="spinner-border spinner-border-sm text-primary" role="status">
                      <span class="visually-hidden">Loading...</span>
                    </div>
                  </div>
                ) : error ? (
                  <div class="alert alert-danger small mb-0" role="alert">
                    {error}
                    <button class="btn btn-sm btn-link p-0 ms-2" onClick={load}>Retry</button>
                  </div>
                ) : filtered.length === 0 ? (
                  <p class="text-body-secondary small text-center mb-0 py-3">No character cards found</p>
                ) : (
                  <div class="d-flex flex-column gap-1" style="max-height: 60vh; overflow-y: auto;">
                    {filtered.map((c) => {
                      const nameColor = getClassColor(c.class);
                      const isSel = selected === c.guid;
                      return (
                        <button
                          key={c.guid}
                          type="button"
                          class={`btn btn-sm text-start ${isSel ? 'btn-primary' : 'btn-outline-secondary'}`}
                          onClick={() => setSelected(c.guid)}
                        >
                          <div class="d-flex align-items-center gap-2">
                            <span class="fw-bold" style={!isSel && nameColor ? `color: ${nameColor}` : undefined}>{c.name}</span>
                            {c.level > 0 && <span class="badge bg-secondary">{c.level}</span>}
                            {c.owned && <span class="badge bg-success ms-auto">Yours</span>}
                          </div>
                          <div class="small opacity-75">
                            <i class={`bi ${genderIcon[c.gender] || ''}`}></i> {c.race} {c.class}
                            {' · '}{provLabel(c.provenance)}{c.pinned ? ' · Pinned' : ''}
                          </div>
                        </button>
                      );
                    })}
                  </div>
                )}
              </div>

              {/* Detail */}
              <div class="col-12 col-md-7">
                {selected ? (
                  <CardSection
                    key={selected}
                    token={token}
                    guid={selected}
                    open={true}
                    reloadKey={detailReloadKey}
                    canRegen={canRegenSelected}
                    onDesync={onDesync}
                  />
                ) : (
                  <div class="d-flex justify-content-center align-items-center text-body-secondary small" style="min-height: 8rem;">
                    Select a character to view its card
                  </div>
                )}
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
