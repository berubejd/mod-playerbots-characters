export default function LoadingView({ steps, error, onRetry, onOpenAccountManager }) {
  return (
    <div class="d-flex justify-content-center align-items-center min-vh-100">
      <div style="max-width: 320px; width: 100%;">
        <div class="text-start mb-3">
          {steps.map((step) => (
            <div key={step.key} class="d-flex align-items-center mb-2">
              {step.status === 'done' && (
                <span class="text-success me-2" style="width: 20px; text-align: center;">✓</span>
              )}
              {step.status === 'active' && (
                <div class="spinner-border spinner-border-sm text-primary me-2" role="status" style="width: 20px; height: 20px;">
                  <span class="visually-hidden">Loading...</span>
                </div>
              )}
              {step.status === 'error' && (
                <span class="text-danger me-2" style="width: 20px; text-align: center;">✗</span>
              )}
              {step.status === 'pending' && (
                <span class="me-2" style="width: 20px; text-align: center; color: var(--bs-secondary-color);">·</span>
              )}
              <span class={
                step.status === 'pending' ? 'text-body-secondary' :
                step.status === 'error' ? 'text-danger' : ''
              }>{step.label}</span>
            </div>
          ))}
        </div>
        {error && (
          <div class="text-center">
            <div class="alert alert-danger" role="alert">
              {error}
            </div>
            <button class="btn btn-primary" onClick={onRetry}>
              Retry
            </button>
          </div>
        )}
        {onOpenAccountManager && (
          <div class="text-center mt-4">
            <button
              class="btn btn-link text-body-secondary text-decoration-none small"
              onClick={onOpenAccountManager}
              type="button"
            >
              Account Manager
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
