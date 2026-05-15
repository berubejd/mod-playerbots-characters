import { useState, useMemo, useCallback, useEffect, useRef } from 'preact/hooks';
import CharacterCard from './character-card.jsx';
import ChatView from './chat-view.jsx';
import CharacterInfo from './character-info.jsx';
import { getClassColor } from './wow-colors.js';
import { useMediaQuery } from './use-media-query.js';
import { useToast } from './toast-provider.jsx';
import { useVisualViewport } from './use-visual-viewport.js';

const TAB = { CHARACTERS: 'characters', CHAT: 'chat', INFO: 'info' };

export default function PlayerView({ player, party, token, faction, wsEvent, onSubscriptionChange, initialSelectedGuid, onDesync, onOpenAccountManager, maxHistoryCtx }) {
  const characters = (party?.party || []).filter((m) => m.character);
  const [messageMode, setMessageMode] = useState('whisper');

  // Initialize selectedGuid from initialSelectedGuid if it's a valid character
  const [selectedGuid, setSelectedGuid] = useState(() => {
    if (!initialSelectedGuid) return null;
    return characters.some(c => c.guid === initialSelectedGuid) ? initialSelectedGuid : null;
  });
  const [activeTab, setActiveTab] = useState(TAB.CHARACTERS);
  const isMobile = useMediaQuery('(max-width: 767px)');
  const toast = useToast();
  useVisualViewport();

  // Track which guid has completed its initial chat data load.
  // subscribeReady is derived: true only when loadedGuid matches selectedGuid.
  // This naturally resets to false when selectedGuid changes (loadedGuid lags behind).
  const [loadedGuid, setLoadedGuid] = useState(null);
  const selectedGuidRef = useRef(null);
  selectedGuidRef.current = selectedGuid;

  const subscribeReady = loadedGuid === selectedGuid && selectedGuid !== null;

  // Notify App about subscription param changes
  useEffect(() => {
    onSubscriptionChange({ selectedGuid, subscribeReady });
  }, [selectedGuid, subscribeReady, onSubscriptionChange]);

  // WS event-driven state
  const [chatEvent, setChatEvent] = useState(null);     // { type, data, timestamp } — only history/thinks
  const [chatReloadKey, setChatReloadKey] = useState(0);
  const [infoReloadKey, setInfoReloadKey] = useState(0);

  // Process WS events forwarded from App
  useEffect(() => {
    if (!wsEvent) return;

    switch (wsEvent.type) {
      case 'connected':
        toast('Server connection established', 'success');
        break;
      case 'error':
        toast(wsEvent.data.message || 'WebSocket error', 'error');
        break;
      case 'history':
        // Forward to ChatView for processing and refresh context
        setChatEvent(wsEvent);
        setInfoReloadKey((k) => k + 1);
        break;
      case 'thinks':
        // Forward to ChatView for processing
        setChatEvent(wsEvent);
        break;
      case 'relationship':
        // Re-request character info (includes relationships)
        setInfoReloadKey((k) => k + 1);
        break;
      case 'memory': {
        // Toast notification — no need to re-request data
        const memCharName = selectedCharName || 'character';
        toast(`The memory of ${memCharName} was updated`, 'success');
        break;
      }
      // subscribed/unsubscribed are internal only, no action needed
    }
  }, [wsEvent, toast]);

  // Build a map of name → class color for chat highlighting and info display
  const nameColorMap = useMemo(() => {
    const map = {};
    const playerColor = getClassColor(player['class']);
    if (playerColor) map[player.name] = playerColor;
    for (const char of characters) {
      const color = getClassColor(char['class']);
      if (color) map[char.name] = color;
    }
    return map;
  }, [player, characters]);

  // Get the name of the currently selected character (for thinking indicator)
  const selectedCharName = useMemo(() => {
    if (!selectedGuid) return null;
    const char = characters.find((c) => c.guid === selectedGuid);
    return char ? char.name : null;
  }, [selectedGuid, characters]);

  const handleSelect = useCallback((guid) => {
    if (guid !== selectedGuid) {
      setSelectedGuid(guid);
      // On mobile, switch to chat tab when selecting a character
      if (isMobile) {
        setActiveTab(TAB.CHAT);
      }
    }
  }, [selectedGuid, isMobile]);

  // Called by ChatView when initial history load completes successfully
  const handleChatLoadComplete = useCallback(() => {
    setLoadedGuid(selectedGuidRef.current);
  }, []);

  // --- Shared sub-renders ---

  const factionIcon = faction ? `/images/${faction}.png` : null;

  const playerInfoBar = (
    <div class="p-3 border-bottom player-info-bar d-flex align-items-center justify-content-between">
      <div style={isMobile ? undefined : 'width: 25%'}>
        <CharacterCard
          name={player.name}
          level={player.level}
          gender={player.gender}
          race={player.race}
          cls={player['class']}
        />
      </div>
      {factionIcon && (
        <img
          src={factionIcon}
          alt={faction === 'horde' ? 'Horde' : 'Alliance'}
          class="faction-icon"
          width="48"
          height="48"
          onClick={onOpenAccountManager}
          style="cursor: pointer"
          title="Account Manager"
        />
      )}
    </div>
  );

  const charactersList = (
    <div class={isMobile ? 'overflow-auto flex-grow-1 p-2' : 'border-end overflow-auto p-2'} style={isMobile ? 'min-height: 0' : 'width: 20%'}>
      <div class="d-flex flex-column gap-2">
        {characters.length === 0 && (
          <p class="text-body-secondary text-center mb-0 small">No characters in party</p>
        )}
        {characters.map((char) => (
          <CharacterCard
            key={char.guid}
            name={char.name}
            level={char.level}
            gender={char.gender}
            race={char.race}
            cls={char['class']}
            selected={selectedGuid === char.guid}
            onClick={() => handleSelect(char.guid)}
          />
        ))}
      </div>
    </div>
  );

  const chatArea = selectedGuid ? (
    <ChatView
      key={selectedGuid}
      token={token}
      selectedGuid={selectedGuid}
      nameColorMap={nameColorMap}
      charName={selectedCharName}
      playerName={player.name}
      chatEvent={chatEvent}
      chatReloadKey={chatReloadKey}
      onLoadComplete={handleChatLoadComplete}
      onDesync={onDesync}
      characters={characters}
      messageMode={messageMode}
      onMessageModeChange={setMessageMode}
      maxHistoryCtx={maxHistoryCtx}
    />
  ) : (
    <div class="d-flex justify-content-center align-items-center h-100">
      <p class="text-body-secondary">Select a character</p>
    </div>
  );

  const infoArea = selectedGuid ? (
    <CharacterInfo
      key={selectedGuid}
      token={token}
      selectedGuid={selectedGuid}
      nameColorMap={nameColorMap}
      charName={selectedCharName}
      reloadKey={infoReloadKey}
      onDesync={onDesync}
    />
  ) : (
    <div class="d-flex justify-content-center align-items-center h-100">
      <p class="text-body-secondary small text-center">Select a character to view info</p>
    </div>
  );

  // --- Mobile layout: tabs ---

  if (isMobile) {
    return (
      <div class="d-flex flex-column dvh-100">
        {playerInfoBar}
        <ul class="nav nav-tabs nav-justified flex-shrink-0" role="tablist">
          <li class="nav-item" role="presentation">
            <button
              class={`nav-link ${activeTab === TAB.CHARACTERS ? 'active' : ''}`}
              onClick={() => setActiveTab(TAB.CHARACTERS)}
              type="button"
            >
              Characters
            </button>
          </li>
          <li class="nav-item" role="presentation">
            <button
              class={`nav-link ${activeTab === TAB.CHAT ? 'active' : ''}`}
              onClick={() => setActiveTab(TAB.CHAT)}
              type="button"
            >
              Chat
            </button>
          </li>
          <li class="nav-item" role="presentation">
            <button
              class={`nav-link ${activeTab === TAB.INFO ? 'active' : ''}`}
              onClick={() => setActiveTab(TAB.INFO)}
              type="button"
              disabled={!selectedGuid}
            >
              Info
            </button>
          </li>
        </ul>
        <div class="d-flex flex-column flex-grow-1" style="min-height: 0">
          {activeTab === TAB.CHARACTERS && charactersList}
          {activeTab === TAB.CHAT && chatArea}
          {activeTab === TAB.INFO && <div class="overflow-auto flex-grow-1" style="min-height: 0">{infoArea}</div>}
        </div>
      </div>
    );
  }

  // --- Desktop layout: 3 columns ---

  return (
    <div class="d-flex flex-column dvh-100">
      {playerInfoBar}
      <div class="d-flex flex-grow-1" style="min-height: 0">
        {charactersList}
        <div style="width: 60%; min-height: 0" class="d-flex flex-column">
          {chatArea}
        </div>
        <div style="width: 20%" class="border-start overflow-auto p-2">
          {infoArea}
        </div>
      </div>
    </div>
  );
}
