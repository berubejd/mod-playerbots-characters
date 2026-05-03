import { useState } from 'preact/hooks';
import { getAccounts, removeAccount } from './account-store.js';
import { getClassColor } from './wow-colors.js';

export default function AccountManager({ onSwitchAccount, onAddAccount }) {
  return (
    <div class="d-flex justify-content-center align-items-center min-vh-100">
      <div style="max-width: 400px; width: 100%;" class="px-3">
        <h5 class="text-center mb-4">Accounts</h5>

        <AccountList onSwitchAccount={onSwitchAccount} onAddAccount={onAddAccount} />
      </div>
    </div>
  );
}

function AccountList({ onSwitchAccount, onAddAccount }) {
  const [version, setVersion] = useState(0);
  const accounts = getAccounts();

  function handleDelete(e, token) {
    e.stopPropagation();
    removeAccount(token);
    setVersion(v => v + 1);
  }

  return (
    <>
      {accounts.length === 0 && (
        <p class="text-body-secondary text-center mb-4">No saved accounts</p>
      )}

      <div class="d-flex flex-column gap-2 mb-3">
        {accounts.map((account) => {
          const nameColor = getClassColor(account.cls);
          const nameStyle = nameColor ? `color: ${nameColor}` : undefined;
          const factionIcon = account.faction ? `/images/${account.faction}.png` : null;

          return (
            <div
              key={account.token}
              class="card"
              style="cursor: pointer"
              onClick={() => onSwitchAccount(account.token)}
            >
              <div class="card-body py-2 px-3 d-flex align-items-center justify-content-between">
                <div class="d-flex align-items-center gap-2 flex-grow-1" style="min-width: 0">
                  {factionIcon && (
                    <img
                      src={factionIcon}
                      alt={account.faction === 'horde' ? 'Horde' : 'Alliance'}
                      width="28"
                      height="28"
                      class="flex-shrink-0"
                      style="opacity: 0.85; image-rendering: auto;"
                    />
                  )}
                  <div style="min-width: 0">
                    <div class="d-flex align-items-center gap-2">
                      <span class="fw-bold text-truncate" style={nameStyle}>{account.name}</span>
                      <span class="badge bg-secondary flex-shrink-0">{account.level}</span>
                    </div>
                    <small class="text-body-secondary text-truncate d-block">{account.gender} {account.race} {account.cls}</small>
                  </div>
                </div>
                <button
                  class="btn btn-link text-danger p-0 ms-2 flex-shrink-0"
                  onClick={(e) => handleDelete(e, account.token)}
                  type="button"
                  title="Remove account"
                >
                  <i class="bi bi-trash3"></i>
                </button>
              </div>
            </div>
          );
        })}
      </div>

      <button
        class="btn btn-outline-primary w-100 d-flex align-items-center justify-content-center gap-2"
        onClick={onAddAccount}
        type="button"
      >
        <i class="bi bi-plus-lg"></i> Add Account
      </button>
    </>
  );
}
