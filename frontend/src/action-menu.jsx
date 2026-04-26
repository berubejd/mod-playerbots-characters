import { useState, useEffect, useRef, useCallback } from 'preact/hooks';

/**
 * Three-dot dropdown menu for message/item actions (Edit, Delete).
 *
 * On touch devices this replaces the hover-revealed inline buttons.
 * The component is hidden on hover-capable devices via CSS (.action-menu { display: none })
 * and shown on touch devices via @media (hover: none).
 *
 * The dropdown auto-positions above or below the toggle button based on
 * available viewport space, and elevates its z-index when open so it
 * always appears above other message lines.
 */
export default function ActionMenu({ onEdit, onDelete }) {
  const [open, setOpen] = useState(false);
  const [dropUp, setDropUp] = useState(false);
  const menuRef = useRef(null);
  const toggleRef = useRef(null);

  // Determine whether to show the dropdown above or below the toggle
  const updatePosition = useCallback(() => {
    if (!toggleRef.current) return;
    const rect = toggleRef.current.getBoundingClientRect();
    // If the toggle is in the bottom 40% of the viewport, drop up
    setDropUp(rect.bottom > window.innerHeight * 0.6);
  }, []);

  // Close on outside click/tap
  useEffect(() => {
    if (!open) return;
    const handle = (e) => {
      if (menuRef.current && !menuRef.current.contains(e.target)) {
        setOpen(false);
      }
    };
    document.addEventListener('mousedown', handle);
    document.addEventListener('touchstart', handle, { passive: true });
    return () => {
      document.removeEventListener('mousedown', handle);
      document.removeEventListener('touchstart', handle);
    };
  }, [open]);

  const handleToggle = (e) => {
    e.stopPropagation();
    if (!open) updatePosition();
    setOpen(!open);
  };

  const handleEdit = (e) => {
    e.stopPropagation();
    setOpen(false);
    onEdit();
  };

  const handleDelete = (e) => {
    e.stopPropagation();
    setOpen(false);
    onDelete();
  };

  return (
    <div ref={menuRef} class="action-menu-inner" style={open ? 'position: relative; z-index: 1000;' : undefined}>
      <button
        ref={toggleRef}
        class="btn btn-sm p-0 px-1 action-menu-toggle"
        title="Actions"
        onClick={handleToggle}
      >
        <i class="bi bi-three-dots-vertical"></i>
      </button>
      {open && (
        <div class={`action-menu-dropdown ${dropUp ? 'dropup' : 'dropdown'}`}>
          <button class="dropdown-item" onClick={handleEdit}>
            <i class="bi bi-pencil me-2"></i>Edit
          </button>
          <button class="dropdown-item text-danger" onClick={handleDelete}>
            <i class="bi bi-trash3 me-2"></i>Delete
          </button>
        </div>
      )}
    </div>
  );
}
