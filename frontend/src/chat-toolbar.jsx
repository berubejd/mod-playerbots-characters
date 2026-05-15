import { useState, useEffect, useCallback, useRef } from 'preact/hooks';
import { fetchDebugRequest, fetchCharData, fetchMemoryCount, updateCharData, fetchMemories, editMemory, deleteMemory, formatApiError } from './api.js';
import { useToast } from './toast-provider.jsx';

// --- Constants ---

export const IMPORTANCE_LABELS = {
  1: 'trivial',
  2: 'minor',
  3: 'insignificant',
  4: 'not a big deal',
  5: 'ordinary',
  6: 'noteworthy',
  7: 'important',
  8: 'very important',
  9: 'critical',
  10: 'life-changing',
};

export function importanceBadge(level) {
  const label = IMPORTANCE_LABELS[level] || level;
  let cls = 'bg-secondary';
  if (level >= 9) cls = 'bg-danger';
  else if (level >= 7) cls = 'bg-warning text-dark';
  else if (level >= 5) cls = 'bg-info text-dark';
  else if (level >= 3) cls = 'bg-primary';
  return <span class={`badge ${cls}`}>{level} – {label}</span>;
}

// --- Delete confirmation modal ---

export function DeleteModal({ show, onConfirm, onCancel }) {
  if (!show) return null;

  return (
    <div class="modal d-block" tabindex="-1" style="background: rgba(0,0,0,0.5)" onClick={onCancel}>
      <div class="modal-dialog modal-dialog-centered" onClick={(e) => e.stopPropagation()}>
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title">Delete</h5>
            <button type="button" class="btn-close" onClick={onCancel}></button>
          </div>
          <div class="modal-body">
            <p class="mb-0">Are you sure? This action is irreversible.</p>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-danger" onClick={onConfirm}>Delete</button>
          </div>
        </div>
      </div>
    </div>
  );
}

// --- Roll modifier edit modal ---

export function RollModifierModal({ show, value, charName, onSave, onCancel }) {
  const [val, setVal] = useState(value);
  const inputRef = useRef(null);

  useEffect(() => {
    if (show) {
      setVal(value);
      requestAnimationFrame(() => {
        if (inputRef.current) inputRef.current.focus();
      });
    }
  }, [show, value]);

  const handleKeyDown = (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      onSave(parseInt(val, 10) || 0);
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
            <h5 class="modal-title">Edit Roll Modifier</h5>
            <button type="button" class="btn-close" onClick={onCancel}></button>
          </div>
          <div class="modal-body">
            <p class="small text-body-secondary mb-2">
              The roll modifier adjusts the likelihood of {charName || 'the character'} responding to events.
              A positive value makes responses more likely, a negative value makes them less likely.
              Range: -100 to 100.
            </p>
            <input
              ref={inputRef}
              type="number"
              class="form-control"
              min="-100"
              max="100"
              value={val}
              onInput={(e) => setVal(e.target.value)}
              onKeyDown={handleKeyDown}
            />
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-primary" onClick={() => onSave(parseInt(val, 10) || 0)}>Save</button>
          </div>
        </div>
      </div>
    </div>
  );
}

// --- Memory edit modal ---

export function MemoryEditModal({ show, memory, onSave, onCancel }) {
  const [text, setText] = useState('');
  const [importance, setImportance] = useState(5);
  const inputRef = useRef(null);

  const adjustHeight = useCallback(() => {
    const el = inputRef.current;
    if (!el) return;
    el.style.height = 'auto';
    el.style.height = el.scrollHeight + 'px';
  }, []);

  useEffect(() => {
    if (show && memory) {
      setText(memory.memory_text || '');
      setImportance(memory.importance || 5);
      requestAnimationFrame(() => {
        if (inputRef.current) {
          inputRef.current.focus();
          adjustHeight();
        }
      });
    }
  }, [show, memory, adjustHeight]);

  const handleKeyDown = (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      onSave(text, importance);
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
            <h5 class="modal-title">Edit Memory</h5>
            <button type="button" class="btn-close" onClick={onCancel}></button>
          </div>
          <div class="modal-body">
            <div class="mb-3">
              <label class="form-label small fw-bold">Importance</label>
              <select
                class="form-select form-select-sm"
                value={importance}
                onChange={(e) => setImportance(parseInt(e.target.value, 10))}
              >
                {Object.entries(IMPORTANCE_LABELS).map(([v, label]) => (
                  <option key={v} value={v}>{v} – {label}</option>
                ))}
              </select>
            </div>
            <div class="mb-0">
              <label class="form-label small fw-bold">Memory</label>
              <textarea
                ref={inputRef}
                class="form-control form-control-sm"
                rows="1"
                value={text}
                onInput={(e) => {
                  const v = e.target.value.replace(/\n/g, '');
                  setText(v);
                  adjustHeight();
                }}
                onKeyDown={handleKeyDown}
                style="resize: none; min-height: 38px; overflow: hidden;"
              />
            </div>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" onClick={onCancel}>Cancel</button>
            <button type="button" class="btn btn-primary" onClick={() => onSave(text, importance)}>Save</button>
          </div>
        </div>
      </div>
    </div>
  );
}

// --- Memories browse modal ---

export function MemoriesModal({ show, token, guid, charName, onClose, onDesync }) {
  const [memories, setMemories] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [page, setPage] = useState(1);
  const [totalPages, setTotalPages] = useState(1);
  const [total, setTotal] = useState(0);
  const [orderBy, setOrderBy] = useState('id');
  const [orderDir, setOrderDir] = useState('desc');
  const [editMemoryModal, setEditMemoryModal] = useState({ show: false, memory: null });
  const [deleteMemoryModal, setDeleteMemoryModal] = useState({ show: false, memory: null });
  const toast = useToast();
  const PAGE_SIZE = 10;

  const loadMemories = useCallback(async (p, ob, od) => {
    if (!token || !guid) return;
    setLoading(true);
    setError(null);
    try {
      const data = await fetchMemories(token, guid, {
        order_by: ob,
        order_dir: od,
        page: p,
        limit: PAGE_SIZE,
      });
      setMemories(data.memories || []);
      setTotalPages(data.total_pages || 1);
      setTotal(data.total || 0);
      setPage(p);
    } catch (err) {
      if (err.message === 'player_offline') {
        onDesync('player_offline');
        return;
      }
      setError(formatApiError(err));
    } finally {
      setLoading(false);
    }
  }, [token, guid, onDesync]);

  useEffect(() => {
    if (show) {
      loadMemories(1, orderBy, orderDir);
    }
  }, [show]);

  const handleSort = useCallback((column) => {
    const newDir = (orderBy === column && orderDir === 'asc') ? 'desc' : 'asc';
    setOrderBy(column);
    setOrderDir(newDir);
    loadMemories(1, column, newDir);
  }, [orderBy, orderDir, loadMemories]);

  const handlePageChange = useCallback((newPage) => {
    loadMemories(newPage, orderBy, orderDir);
  }, [orderBy, orderDir, loadMemories]);

  const handleEditOpen = useCallback((mem) => {
    setEditMemoryModal({ show: true, memory: mem });
  }, []);

  const handleEditSave = useCallback(async (newText, newImportance) => {
    const mem = editMemoryModal.memory;
    if (!mem) return;
    try {
      await editMemory(token, guid, mem.id, newText, newImportance, mem.memory_text);
      toast('Memory updated', 'success');
      setEditMemoryModal({ show: false, memory: null });
      loadMemories(page, orderBy, orderDir);
    } catch (err) {
      if (err.message === 'desync' || err.message === 'player_offline') {
        onDesync(err.message);
        return;
      }
      toast('Failed to update memory', 'error');
    }
  }, [token, guid, editMemoryModal, toast, onDesync, page, orderBy, orderDir, loadMemories]);

  const handleEditCancel = useCallback(() => {
    setEditMemoryModal({ show: false, memory: null });
  }, []);

  const handleDeleteOpen = useCallback((mem) => {
    setDeleteMemoryModal({ show: true, memory: mem });
  }, []);

  const handleDeleteConfirm = useCallback(async () => {
    const mem = deleteMemoryModal.memory;
    if (!mem) return;
    try {
      await deleteMemory(token, guid, mem.id, mem.memory_text);
      toast('Memory deleted', 'success');
      setDeleteMemoryModal({ show: false, memory: null });
      loadMemories(page, orderBy, orderDir);
    } catch (err) {
      if (err.message === 'desync' || err.message === 'player_offline') {
        onDesync(err.message);
        return;
      }
      toast('Failed to delete memory', 'error');
    }
  }, [token, guid, deleteMemoryModal, toast, onDesync, page, orderBy, orderDir, loadMemories]);

  const handleDeleteCancel = useCallback(() => {
    setDeleteMemoryModal({ show: false, memory: null });
  }, []);

  const sortIcon = (col) => {
    if (orderBy !== col) return <i class="bi bi-dash opacity-25 ms-1"></i>;
    return orderDir === 'asc'
      ? <i class="bi bi-sort-up ms-1"></i>
      : <i class="bi bi-sort-down ms-1"></i>;
  };

  if (!show) return null;

  return (
    <div class="modal d-block" tabindex="-1" style="background: rgba(0,0,0,0.5)" onClick={onClose}>
      <div class="modal-dialog modal-lg modal-dialog-centered modal-dialog-scrollable" onClick={(e) => e.stopPropagation()}>
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title">Memories — {charName}</h5>
            <button type="button" class="btn-close" onClick={onClose}></button>
          </div>
          <div class="modal-body p-0">
            {loading ? (
              <div class="text-center py-3">
                <div class="spinner-border spinner-border-sm text-primary" role="status">
                  <span class="visually-hidden">Loading...</span>
                </div>
              </div>
            ) : error ? (
              <div class="alert alert-danger small m-2" role="alert">{error}</div>
            ) : memories && memories.length > 0 ? (
              <>
                <div class="table-responsive">
                  <table class="table table-sm table-hover small mb-0 align-middle">
                    <thead>
                      <tr>
                        <th style="cursor:pointer; white-space:nowrap" onClick={() => handleSort('id')}>
                          ID {sortIcon('id')}
                        </th>
                        <th style="cursor:pointer; white-space:nowrap" onClick={() => handleSort('importance')}>
                          Importance {sortIcon('importance')}
                        </th>
                        <th style="cursor:pointer" onClick={() => handleSort('memory_text')}>
                          Memory {sortIcon('memory_text')}
                        </th>
                        <th style="width: 80px">Actions</th>
                      </tr>
                    </thead>
                    <tbody>
                      {memories.map((mem) => (
                        <tr key={mem.id}>
                          <td class="text-body-secondary">{mem.id}</td>
                          <td>{importanceBadge(mem.importance)}</td>
                          <td style="word-break: break-word; max-width: 400px">{mem.memory_text}</td>
                          <td>
                            <button
                              class="btn btn-sm p-0 px-1"
                              title="Edit"
                              onClick={() => handleEditOpen(mem)}
                            >
                              <i class="bi bi-pencil"></i>
                            </button>
                            <button
                              class="btn btn-sm p-0 px-1"
                              title="Delete"
                              onClick={() => handleDeleteOpen(mem)}
                            >
                              <i class="bi bi-trash3"></i>
                            </button>
                          </td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
                {totalPages > 1 && (
                  <nav class="px-3 py-2">
                    <ul class="pagination pagination-sm mb-0 justify-content-center">
                      <li class={`page-item ${page <= 1 ? 'disabled' : ''}`}>
                        <button class="page-link" onClick={() => handlePageChange(page - 1)}>‹</button>
                      </li>
                      {Array.from({ length: totalPages }, (_, i) => i + 1).map((p) => (
                        <li key={p} class={`page-item ${p === page ? 'active' : ''}`}>
                          <button class="page-link" onClick={() => handlePageChange(p)}>{p}</button>
                        </li>
                      ))}
                      <li class={`page-item ${page >= totalPages ? 'disabled' : ''}`}>
                        <button class="page-link" onClick={() => handlePageChange(page + 1)}>›</button>
                      </li>
                    </ul>
                  </nav>
                )}
              </>
            ) : (
              <p class="text-body-secondary text-center small py-3 mb-0">No memories</p>
            )}
          </div>
        </div>
      </div>
      <MemoryEditModal
        show={editMemoryModal.show}
        memory={editMemoryModal.memory}
        onSave={handleEditSave}
        onCancel={handleEditCancel}
      />
      <DeleteModal
        show={deleteMemoryModal.show}
        onConfirm={handleDeleteConfirm}
        onCancel={handleDeleteCancel}
      />
    </div>
  );
}

// --- Debug Request modal ---

function DebugRequestModal({ show, data, loading, error, onClose }) {
  // Accordion state: which section is open (null = all closed)
  const [openSection, setOpenSection] = useState('general');

  // Reset to general section when modal opens
  useEffect(() => {
    if (show) setOpenSection('general');
  }, [show]);

  if (!show) return null;

  // Threshold for using textarea instead of static text (in characters)
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
            <h5 class="modal-title"><i class="bi bi-eyeglasses me-2"></i>Character Debug</h5>
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

// --- Chat Toolbar ---

export default function ChatToolbar({ token, selectedGuid, charName, isDesktop, selectionMode, selectedIds, onToggleSelectionMode, onBatchDeleteOpen, onDesync }) {
  const toast = useToast();

  // Debug modal state
  const [debugModal, setDebugModal] = useState({ show: false, data: null, loading: false, error: null });

  // Roll modifier modal state
  const [rollModifierModal, setRollModifierModal] = useState(false);

  // Memories modal state
  const [memoriesModal, setMemoriesModal] = useState(false);

  // Character data (roll modifier + memory count)
  const [charData, setCharData] = useState(null);
  const [memoryCount, setMemoryCount] = useState(null);

  // Fetch char data when guid changes
  useEffect(() => {
    if (!token || !selectedGuid) return;
    let cancelled = false;

    Promise.allSettled([
      fetchCharData(token, selectedGuid),
      fetchMemoryCount(token, selectedGuid),
    ]).then(([dataResult, countResult]) => {
      if (cancelled) return;

      for (const result of [dataResult, countResult]) {
        if (result.status === 'rejected' && result.reason && result.reason.message === 'player_offline') {
          onDesync('player_offline');
          return;
        }
      }

      if (dataResult.status === 'fulfilled') {
        setCharData(dataResult.value);
      }
      if (countResult.status === 'fulfilled') {
        setMemoryCount(countResult.value);
      }
    });

    return () => { cancelled = true; };
  }, [token, selectedGuid, onDesync]);

  // Get roll modifier value from charData
  const getRollModifier = useCallback(() => {
    if (!charData || !charData.data) return 0;
    const item = charData.data.find(d => d.key === 'roll_modifier');
    return item ? item.value : 0;
  }, [charData]);

  // Debug request handler
  const handleDebugRequest = useCallback(async () => {
    setDebugModal({ show: true, data: null, loading: true, error: null });
    try {
      const data = await fetchDebugRequest(token, selectedGuid);
      setDebugModal({ show: true, data, loading: false, error: null });
    } catch (err) {
      if (err.message === 'player_offline') {
        onDesync(err.message);
        return;
      }
      setDebugModal({ show: true, data: null, loading: false, error: formatApiError(err) });
    }
  }, [token, selectedGuid, onDesync]);

  const handleDebugClose = useCallback(() => {
    setDebugModal({ show: false, data: null, loading: false, error: null });
  }, []);

  // Roll modifier save handler
  const handleRollModifierSave = useCallback(async (newValue) => {
    const clamped = Math.max(-100, Math.min(100, newValue));
    try {
      await updateCharData(token, selectedGuid, { data: [{ key: 'roll_modifier', value: clamped }] });
      setCharData(prev => {
        if (!prev) return prev;
        return {
          ...prev,
          data: prev.data.map(d => d.key === 'roll_modifier' ? { ...d, value: clamped } : d),
        };
      });
      toast('Roll modifier updated', 'success');
      setRollModifierModal(false);
    } catch (err) {
      if (err.message === 'desync' || err.message === 'player_offline') {
        onDesync(err.message);
        return;
      }
      toast('Failed to update roll modifier', 'error');
    }
  }, [token, selectedGuid, toast, onDesync]);

  const rollMod = getRollModifier();

  return (
    <>
      <div class="d-flex align-items-center gap-2 px-3 py-1 border-bottom bg-body flex-shrink-0">
        {/* Left side: action icons */}
        <button
          class="btn btn-link p-1 text-body"
          onClick={handleDebugRequest}
          title="Character Debug"
        >
          <i class="bi bi-eyeglasses fs-5"></i>
        </button>
        {isDesktop && (
          <button
            class="btn btn-link p-1 text-body"
            style={selectionMode ? 'color: var(--bs-primary) !important;' : undefined}
            onClick={onToggleSelectionMode}
            title={selectionMode ? 'Cancel selection' : 'Select messages'}
          >
            <i class="bi bi-check-square fs-5"></i>
          </button>
        )}
        {isDesktop && selectionMode && selectedIds.size > 0 && (
          <button
            class="btn btn-link p-1"
            style="color: var(--bs-danger) !important;"
            onClick={onBatchDeleteOpen}
            title="Delete selected"
          >
            <i class="bi bi-trash fs-5"></i>
          </button>
        )}

        {/* Right side: roll chance + memories */}
        <div class="ms-auto d-flex align-items-center gap-2">
          <button
            class="btn btn-link p-1 text-body d-flex align-items-center gap-1 text-decoration-none"
            onClick={() => setRollModifierModal(true)}
            title="Edit roll modifier"
          >
            <i class="bi bi-dice-5 fs-5"></i>
            <span class="badge bg-secondary">{rollMod >= 0 ? '+' : ''}{rollMod}</span>
          </button>
          <button
            class="btn btn-link p-1 text-body d-flex align-items-center gap-1 text-decoration-none"
            onClick={() => setMemoriesModal(true)}
            title="Browse memories"
          >
            <i class="bi bi-book fs-5"></i>
            <span class="badge bg-secondary">{memoryCount ? memoryCount.count : 0}</span>
          </button>
        </div>
      </div>

      {/* Modals */}
      <DebugRequestModal
        show={debugModal.show}
        data={debugModal.data}
        loading={debugModal.loading}
        error={debugModal.error}
        onClose={handleDebugClose}
      />
      <RollModifierModal
        show={rollModifierModal}
        value={rollMod}
        charName={charName}
        onSave={handleRollModifierSave}
        onCancel={() => setRollModifierModal(false)}
      />
      <MemoriesModal
        show={memoriesModal}
        token={token}
        guid={selectedGuid}
        charName={charName}
        onClose={() => setMemoriesModal(false)}
        onDesync={onDesync}
      />
    </>
  );
}
