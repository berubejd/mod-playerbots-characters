import { useState, useEffect, useRef, useCallback } from 'preact/hooks';

/**
 * WebSocket hook with automatic reconnection and event subscription.
 *
 * Connects to /ws using the access_token subprotocol for authentication.
 * Reconnects with exponential backoff on disconnect.
 * Automatically subscribes/unsubscribes based on selectedGuid and subscribeReady.
 *
 * @param {string|null} token - Bearer token for authentication. Pass null to disconnect.
 * @param {Object} options
 * @param {number|null} options.selectedGuid - Character GUID to subscribe to. null = no subscription.
 * @param {boolean} options.subscribeReady - Only subscribe when true (after initial data load).
 * @param {Function|null} options.onEvent - Callback for each WS event: ({ type, data, timestamp }) => void
 * @param {Function|null} options.onDisconnect - Callback when WS connection drops unexpectedly: () => void
 * @param {number} options.connectKey - Increment to force reconnection.
 * @returns {{ status: 'disconnected'|'connecting'|'connected' }}
 */
export function useWebSocket(token, { selectedGuid = null, subscribeReady = false, onEvent = null, onDisconnect = null, connectKey = 0 } = {}) {
  const [status, setStatus] = useState('disconnected');
  const wsRef = useRef(null);
  const reconnectTimerRef = useRef(null);
  const reconnectDelayRef = useRef(1000);
  const mountedRef = useRef(true);
  const subscribedGuidRef = useRef(null);

  // Keep refs in sync with latest prop values (updated during render, before effects run)
  const onEventRef = useRef(onEvent);
  onEventRef.current = onEvent;

  const onDisconnectRef = useRef(onDisconnect);
  onDisconnectRef.current = onDisconnect;

  const selectedGuidRef = useRef(selectedGuid);
  selectedGuidRef.current = selectedGuid;

  const subscribeReadyRef = useRef(subscribeReady);
  subscribeReadyRef.current = subscribeReady;

  const clearReconnect = useCallback(() => {
    if (reconnectTimerRef.current !== null) {
      clearTimeout(reconnectTimerRef.current);
      reconnectTimerRef.current = null;
    }
  }, []);

  /**
   * Send subscribe/unsubscribe messages based on current refs.
   * Called from onopen and from the subscription effect.
   */
  const updateSubscription = useCallback(() => {
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return;

    const guid = selectedGuidRef.current;
    const ready = subscribeReadyRef.current;
    const currentSub = subscribedGuidRef.current;

    // Already subscribed to the correct guid — nothing to do
    if (currentSub === guid) return;

    // Unsubscribe from previous if different
    if (currentSub !== null) {
      ws.send('unsubscribe');
      subscribedGuidRef.current = null;
    }

    // Subscribe to new guid if ready
    if (guid !== null && ready) {
      ws.send(`subscribe ${guid}`);
      subscribedGuidRef.current = guid;
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
    subscribedGuidRef.current = null;

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
      reconnectDelayRef.current = 1000; // Reset backoff on successful connection
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
        subscribedGuidRef.current = data.guid;
      } else if (eventType === 'unsubscribed') {
        subscribedGuidRef.current = null;
      }

      // Forward all events to the callback (including subscribed/unsubscribed
      // so the consumer can track subscription state if desired)
      if (onEventRef.current) {
        onEventRef.current({ type: eventType, data, timestamp: Date.now() });
      }
    };

    ws.onclose = () => {
      if (!mountedRef.current) return;
      setStatus('disconnected');
      wsRef.current = null;
      subscribedGuidRef.current = null;

      // Notify about unexpected disconnection
      if (onDisconnectRef.current) {
        onDisconnectRef.current();
      }

      // Schedule reconnect with exponential backoff (1s → 2s → 4s → 8s, max 30s)
      const delay = reconnectDelayRef.current;
      reconnectDelayRef.current = Math.min(delay * 2, 30000);

      reconnectTimerRef.current = setTimeout(() => {
        if (mountedRef.current && token) {
          connect();
        }
      }, delay);
    };

    ws.onerror = () => {
      // onclose will fire after onerror, so reconnection is handled there
    };
  }, [token, updateSubscription]);

  // Update subscription when selectedGuid or subscribeReady changes while connected
  useEffect(() => {
    updateSubscription();
  }, [selectedGuid, subscribeReady, updateSubscription]);

  // Connect when token becomes available, disconnect when removed
  // connectKey forces reconnection when incremented
  useEffect(() => {
    mountedRef.current = true;

    if (token) {
      connect();
    }

    return () => {
      mountedRef.current = false;
      clearReconnect();
      if (wsRef.current) {
        wsRef.current.onopen = null;
        wsRef.current.onclose = null;
        wsRef.current.onmessage = null;
        wsRef.current.onerror = null;
        wsRef.current.close();
        wsRef.current = null;
      }
      setStatus('disconnected');
      subscribedGuidRef.current = null;
    };
  }, [token, connectKey, connect, clearReconnect]);

  return { status };
}
