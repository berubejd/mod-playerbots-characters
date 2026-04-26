import { useRef, useState } from 'preact/hooks';

const OTP_LENGTH = 6;

export default function OtpInput({ onSubmit, error }) {
  const [values, setValues] = useState(() => Array(OTP_LENGTH).fill(''));
  const inputsRef = useRef([]);
  const [submitting, setSubmitting] = useState(false);

  function focusInput(idx) {
    const el = inputsRef.current[idx];
    if (el) el.focus();
  }

  function handleChange(idx, e) {
    const raw = e.target.value;

    // Handle paste: if the full OTP is pasted into one field
    if (raw.length >= OTP_LENGTH) {
      const digits = raw.replace(/\D/g, '').slice(0, OTP_LENGTH).split('');
      const next = [...values];
      digits.forEach((d, i) => { next[i] = d; });
      setValues(next);
      focusInput(Math.min(digits.length, OTP_LENGTH - 1));

      // Auto-submit if we have all digits
      if (digits.length === OTP_LENGTH) {
        submitOtp(next);
      }
      return;
    }

    // Normal single-character input
    const digit = raw.replace(/\D/g, '').slice(-1);
    const next = [...values];
    next[idx] = digit;
    setValues(next);

    if (digit && idx < OTP_LENGTH - 1) {
      focusInput(idx + 1);
    }

    // Auto-submit when the last digit is entered
    if (digit && idx === OTP_LENGTH - 1) {
      const allFilled = next.every((v) => v !== '');
      if (allFilled) {
        submitOtp(next);
      }
    }
  }

  function handleKeyDown(idx, e) {
    if (e.key === 'Backspace' && !values[idx] && idx > 0) {
      focusInput(idx - 1);
    }
  }

  function handleFocus(e) {
    e.target.select();
  }

  async function submitOtp(vals) {
    const otp = (vals || values).join('');
    if (otp.length !== OTP_LENGTH) return;
    setSubmitting(true);
    try {
      await onSubmit(otp);
    } finally {
      setSubmitting(false);
      // Clear on error (parent will set error prop)
    }
  }

  function handleClearError() {
    if (error) {
      setValues(Array(OTP_LENGTH).fill(''));
      focusInput(0);
    }
  }

  // When error appears, clear the form
  if (error && values.some((v) => v !== '')) {
    setTimeout(() => {
      setValues(Array(OTP_LENGTH).fill(''));
      focusInput(0);
    }, 0);
  }

  return (
    <div class="d-flex justify-content-center align-items-center min-vh-100">
      <div class="text-center" style="max-width: 360px; width: 100%;">
        <h4 class="mb-3">Enter your one-time password</h4>
        <p class="text-body-secondary mb-4">
          Use <code>.chars web</code> in-game to get a 6-digit code
        </p>

        <div class="d-flex justify-content-center gap-2 mb-3">
          {values.map((val, idx) => (
            <input
              key={idx}
              ref={(el) => { inputsRef.current[idx] = el; }}
              type="text"
              inputMode="numeric"
              pattern="[0-9]"
              maxlength={OTP_LENGTH}
              class="form-control text-center fw-bold"
              style="width: 3rem; height: 3.5rem; font-size: 1.4rem;"
              value={val}
              onInput={(e) => handleChange(idx, e)}
              onKeyDown={(e) => handleKeyDown(idx, e)}
              onFocus={handleFocus}
              disabled={submitting}
              autocomplete="one-time-code"
            />
          ))}
        </div>

        {error && (
          <div class="alert alert-danger py-2 mt-3" role="alert">
            {error}
          </div>
        )}

        {submitting && (
          <div class="spinner-border spinner-border-sm text-secondary mt-2" role="status">
            <span class="visually-hidden">Loading...</span>
          </div>
        )}
      </div>
    </div>
  );
}
