import { useState, useEffect, useCallback, useRef } from 'preact/hooks';
import { exchangeOtp, fetchPlayer } from './api.js';
import { getToken, setToken as storeToken, removeToken, saveAccount, removeAccount, hasAccounts, getAccounts } from './account-store.js';
import { getFaction } from './wow-colors.js';
import { ToastProvider } from './toast-provider.jsx';
import { useWebSocket } from './use-websocket.js';
import OtpInput from './otp-input.jsx';
import LoadingView from './loading-view.jsx';
import PlayerView from './player-view.jsx';
import AccountManager from './account-manager.jsx';

const VIEW = { OTP: 'otp', LOADING: 'loading', MAIN: 'main', ACCOUNT_MANAGER: 'account_manager' };

const INITIAL_STEPS = [
  { key: 'token', label: 'Validating token', status: 'pending' },
  { key: 'player', label: 'Checking player status', status: 'pending' },
  { key: 'ws', label: 'Connecting to server', status: 'pending' },
];

function getForcedFaction() {
  const hash = window.location.hash;
  if (hash === '#force-theme-alliance') return 'alliance';
  if (hash === '#force-theme-horde') return 'horde';
  return null;
}

function applyFactionTheme(race) {
  const forced = getForcedFaction();
  const faction = forced || getFaction(race);
  const html = document.documentElement;
  if (faction) {
    html.setAttribute('data-faction', faction);
  } else {
    html.removeAttribute('data-faction');
  }
  return faction;
}

function clearFactionTheme() {
  document.documentElement.removeAttribute('data-faction');
}

export default function App() {
  const [view, setView] = useState(null);
  const [player, setPlayer] = useState(null);
  const [faction, setFaction] = useState(null);
  const [otpError, setOtpError] = useState('');
  const [loadError, setLoadError] = useState('');
  const [loadSteps, setLoadSteps] = useState(INITIAL_STEPS);
  const [retryKey, setRetryKey] = useState(0);
  const [wsConnectKey, setWsConnectKey] = useState(0);
  const [lastSelectedGuid, setLastSelectedGuid] = useState(null);

  // Auth token state (synced with localStorage)
  const [authToken, setAuthToken] = useState(() => getToken());

  // WS subscription params (managed by PlayerView via callback)
  const [subscribeParams, setSubscribeParams] = useState({ selectedGuid: null, subscribeReady: false });

  // WS event forwarding to PlayerView
  const [wsEvent, setWsEvent] = useState(null);

  // Track current view in ref for callbacks
  const viewRef = useRef(null);
  viewRef.current = view;

  // Track subscribe params in ref for disconnect handler
  const subscribeParamsRef = useRef(subscribeParams);
  subscribeParamsRef.current = subscribeParams;

  // Guard against multiple disconnect handler invocations
  const handlingDisconnectRef = useRef(false);

  const handleWsEvent = useCallback((event) => {
    setWsEvent(event);
  }, []);

  // Full state reset: clears all app state and returns to the loading view
  // so the entire initialization sequence runs again from scratch.
  const handleFullReset = useCallback(() => {
    // Save current selected guid for restoration after reconnect
    if (subscribeParamsRef.current.selectedGuid) {
      setLastSelectedGuid(subscribeParamsRef.current.selectedGuid);
    }

    setPlayer(null);
    setLoadSteps(INITIAL_STEPS.map(s => ({ ...s })));
    setLoadError('');
    setWsEvent(null);
    setView(VIEW.LOADING);
    setSubscribeParams({ selectedGuid: null, subscribeReady: false });
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

  // Desync / player-offline handler: the frontend state no longer matches
  // the server, or the player went offline.  Reset everything and
  // re-initialize from scratch so the loading screen reveals the problem.
  const handleDesync = useCallback((reason) => {
    if (viewRef.current !== VIEW.MAIN) return;
    handleFullReset();
  }, [handleFullReset]);

  const { status: wsStatus } = useWebSocket(authToken, {
    ...subscribeParams,
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

  // Loading process: sequential steps (token validation → player fetch → WS connection)
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

    // Token exists and passes format check
    setLoadSteps(prev => prev.map(s => s.key === 'token' ? { ...s, status: 'done' } : s));

    // Step 2: Fetch player
    setLoadSteps(prev => prev.map(s => s.key === 'player' ? { ...s, status: 'active' } : s));

    fetchPlayer(token)
      .then((data) => {
        if (cancelled) return;
        setPlayer(data);
        setFaction(applyFactionTheme(data.race));
        // Save account info to localStorage for account manager
        saveAccount(token, data);
        setLoadSteps(prev => prev.map(s => s.key === 'player' ? { ...s, status: 'done' } : s));

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
          setLoadSteps(prev => prev.map(s => s.key === 'player' ? { ...s, status: 'error' } : s));
          setLoadError(
            err.message === 'offline'
              ? 'Player is not online'
              : 'Connection error'
          );
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
    setPlayer(null);
    clearFactionTheme();
    setLoadSteps(INITIAL_STEPS.map(s => ({ ...s })));
    setLoadError('');
    setWsEvent(null);
    setSubscribeParams({ selectedGuid: null, subscribeReady: false });
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
        {view === VIEW.MAIN && player && (
          <PlayerView
            player={player}
            token={authToken}
            faction={faction}
            wsEvent={wsEvent}
            onSubscriptionChange={setSubscribeParams}
            initialSelectedGuid={lastSelectedGuid}
            onDesync={handleDesync}
            onOpenAccountManager={handleOpenAccountManager}
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
