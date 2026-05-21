import { useState, useEffect, useCallback, useRef } from 'preact/hooks';
import { exchangeOtp, fetchAccount, fetchParty, fetchConfig } from './api.js';
import { getToken, setToken as storeToken, removeToken, saveAccount, removeAccount, hasAccounts, getAccounts } from './account-store.js';
import { ToastProvider } from './toast-provider.jsx';
import { useWebSocket } from './use-websocket.js';
import OtpInput from './otp-input.jsx';
import LoadingView from './loading-view.jsx';
import PlayerView from './player-view.jsx';
import AccountManager from './account-manager.jsx';

const VIEW = { OTP: 'otp', LOADING: 'loading', MAIN: 'main', ACCOUNT_MANAGER: 'account_manager' };

const INITIAL_STEPS = [
  { key: 'token', label: 'Validating session', status: 'pending' },
  { key: 'account', label: 'Loading account', status: 'pending' },
  { key: 'ws', label: 'Connecting to server', status: 'pending' },
];

function clearFactionTheme() {
  document.documentElement.removeAttribute('data-faction');
}

export default function App() {
  const [view, setView] = useState(null);
  const [account, setAccount] = useState(null);
  const [party, setParty] = useState(null);
  const [config, setConfig] = useState(null);
  const [otpError, setOtpError] = useState('');
  const [loadError, setLoadError] = useState('');
  const [loadSteps, setLoadSteps] = useState(INITIAL_STEPS);
  const [retryKey, setRetryKey] = useState(0);
  const [wsConnectKey, setWsConnectKey] = useState(0);
  const [lastSelectedGuid, setLastSelectedGuid] = useState(null);

  // Auth token state (synced with localStorage)
  const [authToken, setAuthToken] = useState(() => getToken());

  // Track selected character GUID for restoration after reconnect
  const selectedGuidRef = useRef(null);

  // WS event forwarding to PlayerView
  const [wsEvent, setWsEvent] = useState(null);

  // Track current view in ref for callbacks
  const viewRef = useRef(null);
  viewRef.current = view;

  // Guard against multiple disconnect handler invocations
  const handlingDisconnectRef = useRef(false);

  // Debounce timer for account-level events — batches rapid online/offline/party
  // events (e.g. mass logout, party disband) into a single re-fetch.
  const accountRefreshTimerRef = useRef(null);
  const authTokenRef = useRef(authToken);
  authTokenRef.current = authToken;

  const scheduleAccountRefresh = useCallback(() => {
    if (accountRefreshTimerRef.current) {
      clearTimeout(accountRefreshTimerRef.current);
    }
    accountRefreshTimerRef.current = setTimeout(() => {
      accountRefreshTimerRef.current = null;
      const token = authTokenRef.current;
      if (!token) return;
      fetchAccount(token)
        .then((data) => {
          setAccount(data);
          saveAccount(token, data);
        })
        .catch(() => {});
      fetchParty(token)
        .then((partyData) => {
          setParty(partyData);
        })
        .catch(() => { setParty({ party: [] }); });
    }, 500);
  }, []);

  const handleWsEvent = useCallback((event) => {
    // Handle account-level events that trigger data refresh
    switch (event.type) {
      case 'shutdown':
        // Server is shutting down — disconnect WS after 3s and go to loading
        setTimeout(() => {
          setWsConnectKey(k => k + 1);
          setView(VIEW.LOADING);
          setLoadSteps(INITIAL_STEPS.map(s => ({ ...s })));
          setLoadError('Server is restarting');
        }, 3000);
        return; // Don't forward to PlayerView
      case 'online':
      case 'offline':
      case 'party':
        // Batch these — defer re-fetch by 500ms, resetting on each new event
        scheduleAccountRefresh();
        return; // Don't forward to PlayerView
      default:
        // Forward character-level events to PlayerView
        setWsEvent(event);
        break;
    }
  }, [scheduleAccountRefresh]);

  // Full state reset: clears all app state and returns to the loading view
  // so the entire initialization sequence runs again from scratch.
  const handleFullReset = useCallback(() => {
    // Save current selected guid for restoration after reconnect
    if (selectedGuidRef.current) {
      setLastSelectedGuid(selectedGuidRef.current);
    }

    setAccount(null);
    setParty(null);
    setConfig(null);
    setLoadSteps(INITIAL_STEPS.map(s => ({ ...s })));
    setLoadError('');
    setWsEvent(null);
    setView(VIEW.LOADING);
    setWsConnectKey(k => k + 1);
  }, []);

  const handleWsDisconnect = useCallback(() => {
    if (viewRef.current !== VIEW.MAIN) return;
    if (handlingDisconnectRef.current) return;
    handlingDisconnectRef.current = true;

    handleFullReset();

    // Reset guard after state updates are processed
    setTimeout(() => {
      handlingDisconnectRef.current = false;
    }, 0);
  }, [handleFullReset]);

  // Desync / auth-failure handler: the frontend state no longer matches
  // the server, or the session expired.  Reset everything and
  // re-initialize from scratch so the loading screen reveals the problem.
  const handleDesync = useCallback((reason) => {
    if (viewRef.current !== VIEW.MAIN) return;

    if (reason === 'unauthorized') {
      // Session expired — clear token and go to OTP
      removeToken();
      removeAccount(authToken);
      setAuthToken(null);
      clearFactionTheme();
      setView(VIEW.OTP);
      return;
    }

    handleFullReset();
  }, [handleFullReset, authToken]);

  // Only connect WS when in LOADING or MAIN view — no connection needed
  // on OTP or ACCOUNT_MANAGER screens (no account is active yet).
  const wsToken = (view === VIEW.LOADING || view === VIEW.MAIN) ? authToken : null;

  const { status: wsStatus } = useWebSocket(wsToken, {
    onEvent: handleWsEvent,
    onDisconnect: handleWsDisconnect,
    connectKey: wsConnectKey,
  });

  // On mount: if multiple accounts exist, show account picker;
  // otherwise load the single saved account or show OTP
  useEffect(() => {
    const accounts = getAccounts();
    if (accounts.length > 1) {
      setView(VIEW.ACCOUNT_MANAGER);
    } else if (getToken()) {
      setView(VIEW.LOADING);
    } else {
      setView(VIEW.OTP);
    }
  }, []);

  // Loading process: sequential steps (token validation → account fetch → WS connection)
  useEffect(() => {
    if (view !== VIEW.LOADING) return;

    const token = getToken();
    if (!token) {
      setView(VIEW.OTP);
      return;
    }

    let cancelled = false;

    // Step 1: Validate token (instant — getToken already checks format)
    setLoadSteps(prev => prev.map(s => s.key === 'token' ? { ...s, status: 'active' } : s));
    setLoadSteps(prev => prev.map(s => s.key === 'token' ? { ...s, status: 'done' } : s));

    // Step 2: Fetch account
    setLoadSteps(prev => prev.map(s => s.key === 'account' ? { ...s, status: 'active' } : s));

    fetchAccount(token)
      .then((data) => {
        if (cancelled) return;
        setAccount(data);
        // Save account info to localStorage for account manager
        saveAccount(token, data);
        setLoadSteps(prev => prev.map(s => s.key === 'account' ? { ...s, status: 'done' } : s));

        // Step 2b: Fetch party and config in parallel (both non-fatal)
        return Promise.all([
          fetchParty(token)
            .then((partyData) => {
              if (cancelled) return;
              setParty(partyData);
            })
            .catch(() => {
              if (cancelled) return;
              setParty({ party: [] });
            }),
          fetchConfig(token)
            .then((configData) => {
              if (cancelled) return;
              setConfig(configData);
            })
            .catch(() => {
              if (cancelled) return;
              // Config fetch failure is non-fatal
            }),
        ]);
      })
      .then(() => {
        if (cancelled) return;

        // Step 3: Wait for WS connection (handled by separate effect below)
        setLoadSteps(prev => prev.map(s => s.key === 'ws' ? { ...s, status: 'active' } : s));
      })
      .catch((err) => {
        if (cancelled) return;
        if (err.message === 'unauthorized') {
          removeToken();
          removeAccount(token);
          setAuthToken(null);
          clearFactionTheme();
          setView(VIEW.OTP);
        } else {
          setLoadSteps(prev => prev.map(s => s.key === 'account' ? { ...s, status: 'error' } : s));
          setLoadError('Connection error');
        }
      });

    return () => { cancelled = true; };
  }, [view, retryKey]);

  // Step 3: Wait for WS connection to be established
  useEffect(() => {
    if (view !== VIEW.LOADING) return;

    const wsStep = loadSteps.find(s => s.key === 'ws');
    if (!wsStep || wsStep.status !== 'active') return;

    if (wsStatus === 'connected') {
      setLoadSteps(prev => prev.map(s => s.key === 'ws' ? { ...s, status: 'done' } : s));
      // All steps done — transition to main view
      setView(VIEW.MAIN);
    }
  }, [view, loadSteps, wsStatus]);

  // WS connection timeout (15 seconds)
  useEffect(() => {
    if (view !== VIEW.LOADING) return;

    const wsStep = loadSteps.find(s => s.key === 'ws');
    if (!wsStep || wsStep.status !== 'active') return;

    const timer = setTimeout(() => {
      // Only show error if still waiting for WS
      setLoadSteps(prev => {
        const step = prev.find(s => s.key === 'ws');
        if (step && step.status === 'active') {
          return prev.map(s => s.key === 'ws' ? { ...s, status: 'error' } : s);
        }
        return prev;
      });
      setLoadError('WebSocket connection failed');
    }, 15000);

    return () => clearTimeout(timer);
  }, [view, loadSteps]);

  const handleOtpSubmit = useCallback(async (otp) => {
    setOtpError('');
    try {
      const token = await exchangeOtp(otp);
      storeToken(token);
      setAuthToken(token);
      setLoadSteps(INITIAL_STEPS.map(s => ({ ...s })));
      setLoadError('');
      setView(VIEW.LOADING);
    } catch (err) {
      if (err.message === 'invalid') {
        setOtpError('Invalid or expired OTP');
      } else {
        setOtpError('Connection error');
      }
    }
  }, []);

  const handleRetry = useCallback(() => {
    setLoadError('');
    setLoadSteps(INITIAL_STEPS.map(s => ({ ...s })));
    setWsConnectKey(k => k + 1);
    setRetryKey((k) => k + 1);
  }, []);

  // --- Account Manager navigation ---

  const handleOpenAccountManager = useCallback(() => {
    setView(VIEW.ACCOUNT_MANAGER);
  }, []);

  const handleSwitchAccount = useCallback((token) => {
    storeToken(token);
    setAuthToken(token);
    setAccount(null);
    setParty(null);
    setConfig(null);
    clearFactionTheme();
    setLoadSteps(INITIAL_STEPS.map(s => ({ ...s })));
    setLoadError('');
    setWsEvent(null);
    selectedGuidRef.current = null;
    setWsConnectKey(k => k + 1);
    setView(VIEW.LOADING);
  }, []);

  const handleAddAccount = useCallback(() => {
    setOtpError('');
    setView(VIEW.OTP);
  }, []);

  if (view === null) {
    return null;
  }

  return (
    <ToastProvider>
      <div class="d-flex flex-column min-vh-100">
        {view === VIEW.OTP && (
          <OtpInput
            onSubmit={handleOtpSubmit}
            error={otpError}
            showAccountManager={hasAccounts()}
            onOpenAccountManager={handleOpenAccountManager}
          />
        )}
        {view === VIEW.LOADING && (
          <LoadingView
            steps={loadSteps}
            error={loadError}
            onRetry={handleRetry}
            onOpenAccountManager={handleOpenAccountManager}
          />
        )}
        {view === VIEW.MAIN && account && party && (
          <PlayerView
            account={account}
            party={party}
            token={authToken}
            wsEvent={wsEvent}
            onSelectedGuidChange={(guid) => { selectedGuidRef.current = guid; }}
            initialSelectedGuid={lastSelectedGuid}
            onDesync={handleDesync}
            onOpenAccountManager={handleOpenAccountManager}
            maxHistoryCtx={config ? (config.config.find(c => c.key === 'MaxHistoryCtx')?.value ?? 0) : 0}
          />
        )}
        {view === VIEW.ACCOUNT_MANAGER && (
          <AccountManager
            onSwitchAccount={handleSwitchAccount}
            onAddAccount={handleAddAccount}
          />
        )}
      </div>
    </ToastProvider>
  );
}
