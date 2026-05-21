import { useState, useEffect, useCallback, useRef } from 'preact/hooks';
import { fetchCard, fetchRelationships, fetchContext,
         editRelationship, deleteRelationship, formatApiError } from './api.js';
import { formatMessageParts } from './chat-utils.js';
import { useToast } from './toast-provider.jsx';
import ActionMenu from './action-menu.jsx';
import { DeleteModal } from './chat-toolbar.jsx';

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

export default function CharacterInfo({ token, selectedGuid, nameColorMap, charName, reloadKey = 0, onDesync }) {
  const [openSection, setOpenSection] = useState('context');
  const [cardData, setCardData] = useState(null);
  const [relationshipsData, setRelationshipsData] = useState(null);
  const [contextData, setContextData] = useState(null);
  const [loading, setLoading] = useState({});
  const [errors, setErrors] = useState({});
  const [retryKey, setRetryKey] = useState(0);
  const toast = useToast();

  // Edit modal state — for relationships
  const [editModal, setEditModal] = useState({ show: false, type: null, id: null, name: null, text: '' });
  // Delete modal state
  const [deleteModal, setDeleteModal] = useState({ show: false, type: null, id: null, name: null, text: '' });

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

        // Check for auth failure across all results — triggers re-auth
        for (const result of results) {
          if (result.status === 'rejected' && result.reason && result.reason.message === 'unauthorized') {
            onDesync(result.reason.message);
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

  // When selectedGuid changes, clear all data and default to context section
  useEffect(() => {
    setCardData(null);
    setRelationshipsData(null);
    setContextData(null);
    setErrors({});
    setLoading({});
    setOpenSection('context');
  }, [selectedGuid, token]);

  // Fetch the open section when selectedGuid changes or retry is triggered
  useEffect(() => {
    if (openSection && selectedGuid) {
      loadSection(openSection);
    }
  }, [selectedGuid, token, retryKey]);

  // When reloadKey changes (from WS events), reload context and the currently open section
  useEffect(() => {
    if (!selectedGuid || reloadKey <= 0) return;
    loadSection('context');
    if (openSection && openSection !== 'context') {
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
          const existing = prev.relationships[editModal.name];
          const updatedRel = (typeof existing === 'object' && existing !== null)
            ? { ...existing, text: newText }
            : { text: newText, updated_at: '' };
          return {
            ...prev,
            relationships: {
              ...prev.relationships,
              [editModal.name]: updatedRel,
            },
          };
        });
        toast('Relationship updated', 'success');
      }
      setEditModal({ show: false, type: null, id: null, name: null, text: '' });
    } catch (err) {
      if (err.message === 'desync' || err.message === 'unauthorized') {
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
      if (err.message === 'desync' || err.message === 'unauthorized') {
        onDesync(err.message);
        return;
      }
      toast('Failed to delete', 'error');
    }
  }, [token, selectedGuid, deleteModal, toast, onDesync]);

  const handleDeleteCancel = useCallback(() => {
    setDeleteModal({ show: false, type: null, id: null, name: null, text: '' });
  }, []);

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
      title: 'Character Card',
      content: (
        <>
          {loading.card ? renderLoading('card') : (
            <>
              {renderError('card')}
              {cardData && cardData.card ? (
                <div class="small text-body-secondary" style="white-space: pre-wrap; word-break: break-word;">
                  <FormattedText text={cardData.card} />
                </div>
              ) : (
                !errors.card && <span class="text-body-secondary">No data</span>
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
                Object.entries(relationshipsData.relationships).map(([name, rel]) => {
                  const relText = typeof rel === 'string' ? rel : rel.text;
                  const relUpdatedAt = typeof rel === 'string' ? '' : (rel.updated_at || '');
                  return (
                  <div key={name} class="message-line mb-2 position-relative">
                    <div class="message-actions position-absolute top-0 end-0" style="z-index: 1;">
                      <button
                        class="btn btn-sm p-0 px-1"
                        title="Edit"
                        onClick={(e) => { e.stopPropagation(); handleRelationshipEditOpen(name, relText); }}
                      >
                        <i class="bi bi-pencil"></i>
                      </button>
                      <button
                        class="btn btn-sm p-0 px-1"
                        title="Delete"
                        onClick={(e) => { e.stopPropagation(); handleRelationshipDeleteOpen(name, relText); }}
                      >
                        <i class="bi bi-trash3"></i>
                      </button>
                    </div>
                    <div class="action-menu position-absolute top-0 end-0">
                      <ActionMenu onEdit={() => handleRelationshipEditOpen(name, relText)} onDelete={() => handleRelationshipDeleteOpen(name, relText)} />
                    </div>
                    <div class="d-flex justify-content-between align-items-baseline">
                      <span class="small fw-bold" style={nameColorMap && nameColorMap[name] ? `color: ${nameColorMap[name]}` : undefined}>
                        {name}
                      </span>
                      {relUpdatedAt && <span class="small text-body-secondary" style="font-size: 0.75em">{relUpdatedAt}</span>}
                    </div>
                    <div class="small text-body-secondary" style="white-space: pre-wrap; word-break: break-word;">
                      <FormattedText text={relText} />
                    </div>
                  </div>
                  );
                })
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
                      { vars: ['[char_name]', '[char_gender]', '[char_race]', '[char_class]', '[char_level]', '[char_role]'], icon: 'bi-person-circle' },
                      { vars: ['[scene]'], icon: 'bi-compass-fill' },
                      { vars: ['[pet_info]'], icon: 'bi-bookmark-heart-fill' },
                      { vars: ['[combat_status]'], icon: 'bi-shield-fill' },
                      { vars: ['[equipment]'], icon: 'bi-bag-fill' },
                      { vars: ['[char_group]'], icon: 'bi-people-fill' },
                      { vars: ['[char_los]'], icon: 'bi-eye-fill' },
                    ];
                    let icon = 'bi-info-circle-fill';
                    for (const mapping of varIconMap) {
                      if (mapping.vars.some(v => line.includes(v))) {
                        icon = mapping.icon;
                        break;
                      }
                    }
                    // Strip all annotation markers ([variable_name]) from the displayed text
                    const displayLine = line.replace(/\[[a-zA-Z_][a-zA-Z0-9_]*\]/g, '');
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
    </div>
  );
}
