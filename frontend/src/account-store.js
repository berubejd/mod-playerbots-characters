import { getFaction } from './wow-colors.js';

const ACCOUNTS_KEY = 'pbc_accounts';
const ACTIVE_KEY = 'pbc_active_account';

const TOKEN_FORMAT_RE = /^[A-Za-z0-9-_]+$/;

/**
 * Get all saved accounts from localStorage.
 * @returns {Array<{token: string, name: string, level: number, race: string, cls: string, gender: string, faction: string|null}>}
 */
export function getAccounts() {
  try {
    const raw = localStorage.getItem(ACCOUNTS_KEY);
    if (!raw) return [];
    return JSON.parse(raw);
  } catch {
    return [];
  }
}

function saveAccounts(accounts) {
  localStorage.setItem(ACCOUNTS_KEY, JSON.stringify(accounts));
}

/**
 * Get the currently active account token.
 * Returns null if no active token or token format is invalid.
 */
export function getToken() {
  const token = localStorage.getItem(ACTIVE_KEY);
  if (token && !TOKEN_FORMAT_RE.test(token)) {
    localStorage.removeItem(ACTIVE_KEY);
    return null;
  }
  return token;
}

/**
 * Set the currently active account token.
 */
export function setToken(token) {
  localStorage.setItem(ACTIVE_KEY, token);
}

/**
 * Remove the active account token from localStorage.
 */
export function removeToken() {
  localStorage.removeItem(ACTIVE_KEY);
}

/**
 * Save account info after successful player fetch.
 * Updates existing account or adds a new one. Also sets as active.
 */
export function saveAccount(token, playerData) {
  const accounts = getAccounts();
  const faction = getFaction(playerData.race);
  const account = {
    token,
    name: playerData.name,
    level: playerData.level,
    race: playerData.race,
    cls: playerData['class'],
    gender: playerData.gender,
    faction,
  };
  const idx = accounts.findIndex(a => a.token === token);
  if (idx >= 0) {
    accounts[idx] = account;
  } else {
    accounts.push(account);
  }
  saveAccounts(accounts);
  setToken(token);
}

/**
 * Remove an account by token.
 * Also clears active token if it matches the removed account.
 */
export function removeAccount(token) {
  const accounts = getAccounts().filter(a => a.token !== token);
  saveAccounts(accounts);
  if (getToken() === token) {
    removeToken();
  }
}

/**
 * Check if any accounts are saved.
 */
export function hasAccounts() {
  return getAccounts().length > 0;
}
