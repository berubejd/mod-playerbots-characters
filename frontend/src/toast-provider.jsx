import { createContext } from 'preact';
import { useState, useCallback, useContext } from 'preact/hooks';

const ToastContext = createContext(null);

let toastIdCounter = 0;

export function ToastProvider({ children }) {
  const [toasts, setToasts] = useState([]);

  const addToast = useCallback((message, type = 'success') => {
    const id = ++toastIdCounter;
    setToasts((prev) => [...prev, { id, message, type }]);
    setTimeout(() => {
      setToasts((prev) => prev.filter((t) => t.id !== id));
    }, 3000);
  }, []);

  const removeToast = useCallback((id) => {
    setToasts((prev) => prev.filter((t) => t.id !== id));
  }, []);

  return (
    <ToastContext.Provider value={addToast}>
      {children}
      {/* Toast container — fixed at top center */}
      <div class="toast-container position-fixed top-0 start-50 translate-middle-x p-3" style="z-index: 1090;">
        {toasts.map((toast) => (
          <div
            key={toast.id}
            class={`toast show align-items-center border-0 mb-2 ${toast.type === 'error' ? 'text-bg-danger' : 'text-bg-success'}`}
            role="alert"
          >
            <div class="d-flex">
              <div class="toast-body">{toast.message}</div>
              <button
                type="button"
                class="btn-close btn-close-white me-2 m-auto"
                onClick={() => removeToast(toast.id)}
              ></button>
            </div>
          </div>
        ))}
      </div>
    </ToastContext.Provider>
  );
}

export function useToast() {
  const addToast = useContext(ToastContext);
  if (!addToast) {
    throw new Error('useToast must be used within a ToastProvider');
  }
  return addToast;
}
