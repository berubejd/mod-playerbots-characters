import { useState, useEffect, useCallback, useRef } from 'preact/hooks';
import { fetchCard, fetchRelationships, fetchContext, fetchCharData, updateCharData,
         fetchMemoryCount, fetchMemories, editMemory, deleteMemory,
         editRelationship, deleteRelationship, formatApiError } from './api.js';
import { formatMessageParts } from './chat-utils.js';
import { useToast } from './toast-provider.jsx';
import ActionMenu from './action-menu.jsx';

const IMPORTANCE_LABELS = {
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

function importanceBadge(level) {
  const label = IMPORTANCE_LABELS[level] || level;
  let cls = 'bg-secondary';
  if (level >= 9) cls = 'bg-danger';
  else if (level >= 7) cls = 'bg-warning text-dark';
  else if (level >= 5) cls = 'bg-info text-dark';
  else if (level >= 3) cls = 'bg-primary';
  return <span class={`badge ${cls}`}>{level} – {label}</span>;
}

function FormattedText({ text }) {
  const parts = formatMessageParts(text);
  return (
    <>
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
    </>
  );
}

// Reusable edit modal (same pattern as chat-view.jsx)
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
            <h5 class="modal-title">Edit</h5>
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

// Reusable delete confirmation modal
function DeleteModal({ show, onConfirm, onCancel }) {
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

// Roll modifier edit modal
function RollModifierModal({ show, value, charName, onSave, onCancel }) {
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

// Memory edit modal
function MemoryEditModal({ show, memory, onSave, onCancel }) {
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
                {Object.entries(IMPORTANCE_LABELS).map(([val, label]) => (
                  <option key={val} value={val}>{val} – {label}</option>
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

// Memories browse modal
function MemoriesModal({ show, token, guid, charName, onClose, onDesync }) {
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

export default function CharacterInfo({ token, selectedGuid, nameColorMap, charName, reloadKey = 0, onDesync }) {
  const [openSection, setOpenSection] = useState('context');
  const [cardData, setCardData] = useState(null);
  const [relationshipsData, setRelationshipsData] = useState(null);
  const [contextData, setContextData] = useState(null);
  const [charData, setCharData] = useState(null);
  const [memoryCount, setMemoryCount] = useState(null);
  const [loading, setLoading] = useState({});
  const [errors, setErrors] = useState({});
  const [retryKey, setRetryKey] = useState(0);
  const toast = useToast();

  // Edit modal state — for relationships
  const [editModal, setEditModal] = useState({ show: false, type: null, id: null, name: null, text: '' });
  // Delete modal state
  const [deleteModal, setDeleteModal] = useState({ show: false, type: null, id: null, name: null, text: '' });

  // Roll modifier modal
  const [rollModifierModal, setRollModifierModal] = useState(false);
  // Memories modal
  const [memoriesModal, setMemoriesModal] = useState(false);

  // Track the current selectedGuid in a ref so loadSection always uses the latest
  const selectedGuidRef = useRef(selectedGuid);
  selectedGuidRef.current = selectedGuid;

  // Load data for a specific section
  const loadSection = useCallback((section) => {
    const guid = selectedGuidRef.current;
    if (!guid) return;

    setLoading(prev => ({ ...prev, [section]: true }));
    setErrors(prev => { const n = { ...prev }; delete n[section]; return n; });

    const fetches = [];
    const names = [];

    switch (section) {
      case 'card':
        fetches.push(fetchCard(token, guid));
        names.push('card');
        break;
      case 'relationships':
        fetches.push(fetchRelationships(token, guid));
        names.push('relationships');
        break;
      case 'context':
        fetches.push(fetchContext(token, guid));
        names.push('context');
        break;
    }

    Promise.allSettled(fetches)
      .then(results => {
        // Only update state if the guid hasn't changed since we started fetching
        if (selectedGuidRef.current !== guid) return;

        // Check for player_offline across all results — triggers full reload
        for (const result of results) {
          if (result.status === 'rejected' && result.reason && result.reason.message === 'player_offline') {
            onDesync('player_offline');
            return;
          }
        }

        setLoading(prev => ({ ...prev, [section]: false }));
        results.forEach((result, i) => {
          if (result.status === 'fulfilled') {
            switch (names[i]) {
              case 'card': setCardData(result.value); break;
              case 'relationships': setRelationshipsData(result.value); break;
              case 'context': setContextData(result.value); break;
            }
          } else {
            setErrors(prev => ({ ...prev, [names[i]]: formatApiError(result.reason) }));
          }
        });
      });
  }, [token, onDesync]);

  // Load character data (roll modifier + memory count)
  const loadCharData = useCallback(() => {
    const guid = selectedGuidRef.current;
    if (!guid) return;

    setLoading(prev => ({ ...prev, chardata: true }));
    setErrors(prev => { const n = { ...prev }; delete n.chardata; return n; });

    Promise.allSettled([
      fetchCharData(token, guid),
      fetchMemoryCount(token, guid),
    ]).then(([dataResult, countResult]) => {
      if (selectedGuidRef.current !== guid) return;

      for (const result of [dataResult, countResult]) {
        if (result.status === 'rejected' && result.reason && result.reason.message === 'player_offline') {
          onDesync('player_offline');
          return;
        }
      }

      setLoading(prev => ({ ...prev, chardata: false }));
      if (dataResult.status === 'fulfilled') {
        setCharData(dataResult.value);
      } else {
        setErrors(prev => ({ ...prev, chardata: formatApiError(dataResult.reason) }));
      }
      if (countResult.status === 'fulfilled') {
        setMemoryCount(countResult.value);
      } else {
        setErrors(prev => ({ ...prev, memorycount: formatApiError(countResult.reason) }));
      }
    });
  }, [token, onDesync]);

  // When selectedGuid changes, clear all data and default to context section
  useEffect(() => {
    setCardData(null);
    setRelationshipsData(null);
    setContextData(null);
    setCharData(null);
    setMemoryCount(null);
    setErrors({});
    setLoading({});
    setOpenSection('context');
  }, [selectedGuid, token]);

  // Fetch the open section when selectedGuid changes or retry is triggered
  useEffect(() => {
    if (openSection && selectedGuid) {
      loadSection(openSection);
    }
    if (selectedGuid) {
      loadCharData();
    }
  }, [selectedGuid, token, retryKey]);

  // When reloadKey changes (from WS events), reload the currently open section
  useEffect(() => {
    if (openSection && selectedGuid && reloadKey > 0) {
      loadSection(openSection);
    }
  }, [reloadKey]);

  const handleRetry = useCallback(() => {
    setRetryKey((k) => k + 1);
  }, []);

  const toggleSection = useCallback((section) => {
    setOpenSection(prev => {
      if (prev === section) return null;
      // Fetch data for the newly opened section
      loadSection(section);
      return section;
    });
  }, [loadSection]);

  // --- Relationship edit/delete handlers ---

  const handleRelationshipEditOpen = useCallback((name, text) => {
    setEditModal({ show: true, type: 'relationship', id: null, name, text });
  }, []);

  const handleRelationshipDeleteOpen = useCallback((name, text) => {
    setDeleteModal({ show: true, type: 'relationship', id: null, name, text });
  }, []);

  // --- Shared modal handlers ---

  const handleEditSave = useCallback(async (newText) => {
    try {
      if (editModal.type === 'relationship') {
        await editRelationship(token, selectedGuid, editModal.name, newText, editModal.text);
        // Update local state
        setRelationshipsData(prev => {
          if (!prev || !prev.relationships) return prev;
          return {
            ...prev,
            relationships: {
              ...prev.relationships,
              [editModal.name]: newText,
            },
          };
        });
        toast('Relationship updated', 'success');
      }
      setEditModal({ show: false, type: null, id: null, name: null, text: '' });
    } catch (err) {
      if (err.message === 'desync' || err.message === 'player_offline') {
        onDesync(err.message);
        return;
      }
      toast('Failed to update', 'error');
    }
  }, [token, selectedGuid, editModal, toast, onDesync]);

  const handleEditCancel = useCallback(() => {
    setEditModal({ show: false, type: null, id: null, name: null, text: '' });
  }, []);

  const handleDeleteConfirm = useCallback(async () => {
    try {
      if (deleteModal.type === 'relationship') {
        await deleteRelationship(token, selectedGuid, deleteModal.name, deleteModal.text);
        // Update local state
        setRelationshipsData(prev => {
          if (!prev || !prev.relationships) return prev;
          const updated = { ...prev.relationships };
          delete updated[deleteModal.name];
          return { ...prev, relationships: updated };
        });
        toast('Relationship deleted', 'success');
      }
      setDeleteModal({ show: false, type: null, id: null, name: null, text: '' });
    } catch (err) {
      if (err.message === 'desync' || err.message === 'player_offline') {
        onDesync(err.message);
        return;
      }
      toast('Failed to delete', 'error');
    }
  }, [token, selectedGuid, deleteModal, toast, onDesync]);

  const handleDeleteCancel = useCallback(() => {
    setDeleteModal({ show: false, type: null, id: null, name: null, text: '' });
  }, []);

  // --- Roll modifier handler ---

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

  // --- Helper to get roll modifier value ---

  const getRollModifier = useCallback(() => {
    if (!charData || !charData.data) return 0;
    const item = charData.data.find(d => d.key === 'roll_modifier');
    return item ? item.value : 0;
  }, [charData]);

  // Render a section's loading spinner
  const renderLoading = (section) => (
    <div class="text-center py-2">
      <div class="spinner-border spinner-border-sm text-primary" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  );

  // Render a section's error with retry
  const renderError = (sectionKey) => {
    const err = errors[sectionKey];
    if (!err) return null;
    return (
      <div class="alert alert-danger py-1 small mb-1" role="alert">
        {err}
        <button class="btn btn-sm btn-link p-0 ms-2" onClick={handleRetry}>Retry</button>
      </div>
    );
  };

  const sections = [
    {
      key: 'card',
      title: 'Character Info',
      content: (
        <>
          {loading.card || loading.chardata ? renderLoading('card') : (
            <>
              {renderError('card')}
              {renderError('chardata')}
              {/* Additional info: Roll modifier + Memories */}
              <div class="d-flex gap-3 mb-2 align-items-center flex-wrap">
                <span class="small">
                  Roll modifier: <strong>{getRollModifier() >= 0 ? '+' : ''}{getRollModifier()}</strong>
                  <button
                    class="btn btn-sm p-0 px-1 ms-1"
                    title="Edit roll modifier"
                    onClick={() => setRollModifierModal(true)}
                  >
                    <i class="bi bi-pencil"></i>
                  </button>
                </span>
                <span class="small">
                  Memories: <strong>{memoryCount ? memoryCount.count : 0}</strong>
                  <button
                    class="btn btn-sm p-0 px-1 ms-1"
                    title="Browse memories"
                    onClick={() => setMemoriesModal(true)}
                  >
                    <i class="bi bi-pencil"></i>
                  </button>
                </span>
              </div>
              {cardData && cardData.card && (
                <div class="message-line mb-2 position-relative">
                  <span class="small fw-bold">Card</span>
                  <div class="small text-body-secondary" style="white-space: pre-wrap; word-break: break-word;">
                    <FormattedText text={cardData.card} />
                  </div>
                </div>
              )}
              {!cardData && !errors.card && !errors.chardata && (
                <span class="text-body-secondary">No data</span>
              )}
            </>
          )}
        </>
      ),
    },
    {
      key: 'relationships',
      title: 'Relationships',
      content: (
        <>
          {loading.relationships ? renderLoading('relationships') : (
            <>
              {renderError('relationships')}
              {relationshipsData && relationshipsData.relationships && Object.keys(relationshipsData.relationships).length > 0 ? (
                Object.entries(relationshipsData.relationships).map(([name, desc]) => (
                  <div key={name} class="message-line mb-2 position-relative">
                    <div class="message-actions position-absolute top-0 end-0" style="z-index: 1;">
                      <button
                        class="btn btn-sm p-0 px-1"
                        title="Edit"
                        onClick={(e) => { e.stopPropagation(); handleRelationshipEditOpen(name, desc); }}
                      >
                        <i class="bi bi-pencil"></i>
                      </button>
                      <button
                        class="btn btn-sm p-0 px-1"
                        title="Delete"
                        onClick={(e) => { e.stopPropagation(); handleRelationshipDeleteOpen(name, desc); }}
                      >
                        <i class="bi bi-trash3"></i>
                      </button>
                    </div>
                    <div class="action-menu position-absolute top-0 end-0">
                      <ActionMenu onEdit={() => handleRelationshipEditOpen(name, desc)} onDelete={() => handleRelationshipDeleteOpen(name, desc)} />
                    </div>
                    <span class="small fw-bold" style={nameColorMap && nameColorMap[name] ? `color: ${nameColorMap[name]}` : undefined}>
                      {name}
                    </span>
                    <div class="small text-body-secondary" style="white-space: pre-wrap; word-break: break-word;">
                      <FormattedText text={desc} />
                    </div>
                  </div>
                ))
              ) : (
                !errors.relationships && <span class="text-body-secondary">No data</span>
              )}
            </>
          )}
        </>
      ),
    },
    {
      key: 'context',
      title: 'Current Context',
      content: (
        <>
          {loading.context ? renderLoading('context') : (
            <>
              {renderError('context')}
              {contextData && contextData.context ? (
                <div class="mb-0" style="word-break: break-word;">
                  {contextData.context.split('\n').map((line, i, arr) => {
                    // Determine icon based on which template variable is present
                    const varIconMap = [
                      { vars: ['{char_name}', '{char_gender}', '{char_race}', '{char_class}', '{char_level}', '{char_role}'], icon: 'bi-person-circle' },
                      { vars: ['{scene}'], icon: 'bi-compass-fill' },
                      { vars: ['{combat_status}'], icon: 'bi-shield-fill' },
                      { vars: ['{equipment}'], icon: 'bi-bag-fill' },
                      { vars: ['{char_group}'], icon: 'bi-people-fill' },
                      { vars: ['{char_los}'], icon: 'bi-eye-fill' },
                    ];
                    let icon = 'bi-info-circle-fill';
                    for (const mapping of varIconMap) {
                      if (mapping.vars.some(v => line.includes(v))) {
                        icon = mapping.icon;
                        break;
                      }
                    }
                    // Strip all {variable_name} markers from the displayed text
                    const displayLine = line.replace(/\{[a-zA-Z_][a-zA-Z0-9_]*\}/g, '');
                    return (
                      <div key={i}>
                        <div class="d-flex align-items-baseline">
                          <i class={`bi ${icon} me-2 flex-shrink-0`}></i>
                          <p class="mb-0" style="white-space: pre-wrap;">
                            <FormattedText text={displayLine} />
                          </p>
                        </div>
                        {i < arr.length - 1 && <hr class="my-2" />}
                      </div>
                    );
                  })}
                </div>
              ) : (
                !errors.context && <span class="text-body-secondary">No data</span>
              )}
            </>
          )}
        </>
      ),
    },
  ];

  return (
    <div class="accordion">
      {sections.map(({ key, title, content }) => (
        <div key={key} class="accordion-item">
          <h2 class="accordion-header">
            <button
              class={`accordion-button ${openSection !== key ? 'collapsed' : ''}`}
              type="button"
              onClick={() => toggleSection(key)}
            >
              {title}
            </button>
          </h2>
          <div class={`accordion-collapse collapse ${openSection === key ? 'show' : ''}`}>
            <div class="accordion-body small p-2">
              {content}
            </div>
          </div>
        </div>
      ))}
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
      <RollModifierModal
        show={rollModifierModal}
        value={getRollModifier()}
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
    </div>
  );
}
