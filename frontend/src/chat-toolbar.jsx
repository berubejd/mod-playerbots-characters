import { useState, useEffect, useCallback, useRef, useMemo } from 'preact/hooks';
import { fetchCharData, fetchMemoryCount, updateCharData, fetchMemories, editMemory, deleteMemory, regenLast, formatApiError } from './api.js';
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
  const [jumpInput, setJumpInput] = useState('');
  const toast = useToast();
  const PAGE_SIZE = 10;
  const MAX_VISIBLE_PAGES = 5;

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
      if (err.message === 'unauthorized') {
        onDesync(err.message);
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
      if (err.message === 'desync' || err.message === 'unauthorized') {
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
      if (err.message === 'desync' || err.message === 'unauthorized') {
        onDesync(err.message);
        return;
      }
      toast('Failed to delete memory', 'error');
    }
  }, [token, guid, deleteMemoryModal, toast, onDesync, page, orderBy, orderDir, loadMemories]);

  const handleDeleteCancel = useCallback(() => {
    setDeleteMemoryModal({ show: false, memory: null });
  }, []);

  const handleJump = useCallback(() => {
    const target = parseInt(jumpInput, 10);
    if (Number.isFinite(target) && target >= 1 && target <= totalPages) {
      loadMemories(target, orderBy, orderDir);
      setJumpInput('');
    }
  }, [jumpInput, totalPages, orderBy, orderDir, loadMemories]);

  // Compute visible page window (max MAX_VISIBLE_PAGES pages, centered on current page)
  const visiblePages = useMemo(() => {
    if (totalPages <= MAX_VISIBLE_PAGES) {
      return Array.from({ length: totalPages }, (_, i) => i + 1);
    }
    let start = Math.max(1, page - Math.floor(MAX_VISIBLE_PAGES / 2));
    let end = start + MAX_VISIBLE_PAGES - 1;
    if (end > totalPages) {
      end = totalPages;
      start = Math.max(1, end - MAX_VISIBLE_PAGES + 1);
    }
    return Array.from({ length: end - start + 1 }, (_, i) => start + i);
  }, [page, totalPages]);

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
                          <td class="text-body-secondary">
                            {mem.id}
                            {mem.created_at && <><br /><span style="font-size: 0.75em">{mem.created_at}</span></>}
                          </td>
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
                    <div class="d-flex justify-content-center align-items-center gap-1 flex-wrap">
                      <ul class="pagination pagination-sm mb-0">
                        <li class={`page-item ${page <= 1 ? 'disabled' : ''}`}>
                          <button class="page-link" onClick={() => handlePageChange(1)} title="First page">«</button>
                        </li>
                        <li class={`page-item ${page <= 1 ? 'disabled' : ''}`}>
                          <button class="page-link" onClick={() => handlePageChange(page - 1)} title="Previous">‹</button>
                        </li>
                        {visiblePages.map((p) => (
                          <li key={p} class={`page-item ${p === page ? 'active' : ''}`}>
                            <button class="page-link" onClick={() => handlePageChange(p)}>{p}</button>
                          </li>
                        ))}
                        <li class={`page-item ${page >= totalPages ? 'disabled' : ''}`}>
                          <button class="page-link" onClick={() => handlePageChange(page + 1)} title="Next">›</button>
                        </li>
                        <li class={`page-item ${page >= totalPages ? 'disabled' : ''}`}>
                          <button class="page-link" onClick={() => handlePageChange(totalPages)} title="Last page">»</button>
                        </li>
                      </ul>
                      <span class="d-flex align-items-center gap-1 ms-2" style="font-size: 0.875rem;">
                        <span class="text-body-secondary">Page</span>
                        <input
                          type="number"
                          class="form-control form-control-sm"
                          style="width: 60px;"
                          min="1"
                          max={totalPages}
                          value={jumpInput}
                          onInput={(e) => setJumpInput(e.target.value)}
                          onKeyDown={(e) => { if (e.key === 'Enter') handleJump(); }}
                          placeholder={String(page)}
                        />
                        <span class="text-body-secondary">of {totalPages}</span>
                        <button class="btn btn-sm btn-outline-secondary" onClick={handleJump}>Go</button>
                      </span>
                    </div>
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

// --- Chat Toolbar ---

export default function ChatToolbar({ token, selectedGuid, charName, isDesktop, selectionMode, selectedIds, onToggleSelectionMode, onBatchDeleteOpen, onDesync, isOnline }) {
  const toast = useToast();

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
        if (result.status === 'rejected' && result.reason && result.reason.message === 'unauthorized') {
          onDesync(result.reason.message);
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

  // Regenerate last event responses handler
  const [regenerating, setRegenerating] = useState(false);
  const handleRegenLast = useCallback(async () => {
    if (regenerating) return;
    setRegenerating(true);
    try {
      await regenLast(token);
      toast('Regenerating last responses…', 'success');
    } catch (err) {
      if (err.message === 'unauthorized') {
        onDesync(err.message);
        return;
      }
      if (err.message === 'conflict') {
        toast('Cannot regenerate right now', 'error');
      } else if (err.message === 'forbidden' || err.message === 'gone') {
        toast('No real player online in a group', 'error');
      } else {
        toast('Failed to regenerate', 'error');
      }
    } finally {
      setRegenerating(false);
    }
  }, [token, regenerating, toast, onDesync]);

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
      if (err.message === 'desync' || err.message === 'unauthorized') {
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
        {isOnline && (
          <button
            class="btn btn-link p-1 text-body"
            onClick={handleRegenLast}
            disabled={regenerating}
            title="Regenerate Last"
          >
            {regenerating
              ? <span class="spinner-border spinner-border-sm"></span>
              : <i class="bi bi-arrow-clockwise fs-5"></i>}
          </button>
        )}
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
