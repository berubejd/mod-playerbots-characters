import { useState, useEffect, useRef, useCallback } from 'preact/hooks';

/**
 * WebSocket hook with account-level event subscription.
 *
 * Connects to /ws using the access_token subprotocol for authentication.
 * Does NOT auto-reconnect on disconnect — the consumer must increment
 * connectKey (e.g. via a "Retry" button) to trigger a new connection.
 * Automatically sends "subscribe" on connect when subscribeReady is true.
 *
 * @param {string|null} token - Bearer token for authentication. Pass null to disconnect.
 * @param {Object} options
 * @param {boolean} options.subscribeReady - Only subscribe when true (after initial data load).
 * @param {Function|null} options.onEvent - Callback for each WS event: ({ type, data, timestamp }) => void
 * @param {Function|null} options.onDisconnect - Callback when WS connection drops unexpectedly: () => void
 * @param {number} options.connectKey - Increment to force reconnection.
 * @returns {{ status: 'disconnected'|'connecting'|'connected' }}
 */
export function useWebSocket(token, { subscribeReady = false, onEvent = null, onDisconnect = null, connectKey = 0 } = {}) {
  const [status, setStatus] = useState('disconnected');
  const wsRef = useRef(null);
  const mountedRef = useRef(true);
  const subscribedRef = useRef(false);

  // Keep refs in sync with latest prop values (updated during render, before effects run)
  const onEventRef = useRef(onEvent);
  onEventRef.current = onEvent;

  const onDisconnectRef = useRef(onDisconnect);
  onDisconnectRef.current = onDisconnect;

  const subscribeReadyRef = useRef(subscribeReady);
  subscribeReadyRef.current = subscribeReady;

  /**
   * Send subscribe/unsubscribe messages based on current refs.
   * Called from onopen and from the subscription effect.
   */
  const updateSubscription = useCallback(() => {
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return;

    const ready = subscribeReadyRef.current;
    const currentlySubbed = subscribedRef.current;

    if (ready && !currentlySubbed) {
      ws.send('subscribe');
      subscribedRef.current = true;
    } else if (!ready && currentlySubbed) {
      ws.send('unsubscribe');
      subscribedRef.current = false;
    }
  }, []);

  const connect = useCallback(() => {
    if (!token || !mountedRef.current) return;

    // Close any existing connection
    if (wsRef.current) {
      wsRef.current.onopen = null;
      wsRef.current.onclose = null;
      wsRef.current.onmessage = null;
      wsRef.current.onerror = null;
      wsRef.current.close();
      wsRef.current = null;
    }

    // Reset subscription state on new connection
    subscribedRef.current = false;

    setStatus('connecting');

    // Determine WebSocket URL based on current page protocol
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;

    // Authenticate via Sec-WebSocket-Protocol subprotocol header.
    // Tokens use base64url encoding (no '/' or '='), making them valid subprotocol names.
    const ws = new WebSocket(wsUrl, ['access_token', token]);
    wsRef.current = ws;

    ws.onopen = () => {
      if (!mountedRef.current) return;
      setStatus('connected');
      // Try to subscribe after connection
      updateSubscription();
    };

    ws.onmessage = (event) => {
      if (!mountedRef.current) return;

      // Try to parse as JSON; fall back to raw string
      let data;
      try {
        data = JSON.parse(event.data);
      } catch {
        data = event.data;
      }

      // Skip the initial "hello" message from the server
      if (data === 'hello') return;

      const eventType = data.event || 'message';

      // Handle subscription responses internally
      if (eventType === 'subscribed') {
        subscribedRef.current = true;
      } else if (eventType === 'unsubscribed') {
        subscribedRef.current = false;
      }

      // Forward all events to the callback
      if (onEventRef.current) {
        onEventRef.current({ type: eventType, data, timestamp: Date.now() });
      }
    };

    ws.onclose = () => {
      if (!mountedRef.current) return;
      setStatus('disconnected');
      wsRef.current = null;
      subscribedRef.current = false;

      // Notify about unexpected disconnection
      if (onDisconnectRef.current) {
        onDisconnectRef.current();
      }

      // No auto-reconnect — the consumer must increment connectKey to retry
    };

    ws.onerror = () => {
      // onclose will fire after onerror, so cleanup is handled there
    };
  }, [token, updateSubscription]);

  // Update subscription when subscribeReady changes while connected
  useEffect(() => {
    updateSubscription();
  }, [subscribeReady, updateSubscription]);

  // Connect when token becomes available, disconnect when removed
  // connectKey forces reconnection when incremented
  useEffect(() => {
    mountedRef.current = true;

    if (token) {
      connect();
    }

    return () => {
      mountedRef.current = false;
      if (wsRef.current) {
        wsRef.current.onopen = null;
        wsRef.current.onclose = null;
        wsRef.current.onmessage = null;
        wsRef.current.onerror = null;
        wsRef.current.close();
        wsRef.current = null;
      }
      setStatus('disconnected');
      subscribedRef.current = false;
    };
  }, [token, connectKey, connect]);

  return { status };
}
