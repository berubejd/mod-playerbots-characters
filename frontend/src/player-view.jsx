import { useState, useMemo, useCallback, useEffect } from 'preact/hooks';
import CharacterCard from './character-card.jsx';
import ChatView from './chat-view.jsx';
import CharacterInfo from './character-info.jsx';
import { getClassColor, getFaction } from './wow-colors.js';
import { useMediaQuery } from './use-media-query.js';
import { useToast } from './toast-provider.jsx';
import { useVisualViewport } from './use-visual-viewport.js';

const TAB = { CHARACTERS: 'characters', CHAT: 'chat', INFO: 'info' };

function applyFactionTheme(race) {
  const faction = getFaction(race);
  const html = document.documentElement;
  if (faction) {
    html.setAttribute('data-faction', faction);
  } else {
    html.removeAttribute('data-faction');
  }
  return faction;
}

export default function PlayerView({ account, party, token, wsEvent, onSelectedGuidChange, initialSelectedGuid, onDesync, onOpenAccountManager, maxHistoryCtx }) {
  const allCharacters = account.characters || [];
  const partyGuids = party?.party || [];

  // Categorize characters into three groups
  const { partyChars, onlineChars, offlineChars } = useMemo(() => {
    const partySet = new Set(partyGuids);
    const p = [];
    const o = [];
    const f = [];
    for (const char of allCharacters) {
      if (char.is_online && partySet.has(char.guid)) {
        p.push(char);
      } else if (char.is_online) {
        o.push(char);
      } else {
        f.push(char);
      }
    }
    return { partyChars: p, onlineChars: o, offlineChars: f };
  }, [allCharacters, partyGuids]);

  // Player character (the real player)
  const playerChar = useMemo(() => allCharacters.find(c => c.is_player) || null, [allCharacters]);
  const playerGuid = playerChar ? playerChar.guid : null;

  const [messageMode, setMessageMode] = useState('whisper');
  const [selectedGuid, setSelectedGuid] = useState(() => {
    if (!initialSelectedGuid) return null;
    return allCharacters.some(c => c.guid === initialSelectedGuid) ? initialSelectedGuid : null;
  });
  const [activeTab, setActiveTab] = useState(TAB.CHARACTERS);
  const isMobile = useMediaQuery('(max-width: 767px)');
  const toast = useToast();
  useVisualViewport();

  // Notify App about selected GUID changes (for restoration after reconnect)
  useEffect(() => {
    onSelectedGuidChange(selectedGuid);
  }, [selectedGuid, onSelectedGuidChange]);

  // WS event-driven state
  const [chatEvent, setChatEvent] = useState(null);
  const [chatReloadKey, setChatReloadKey] = useState(0);
  const [infoReloadKey, setInfoReloadKey] = useState(0);

  // Track which characters are currently "thinking" (LLM call in progress).
  // Updated for ALL account characters, not just the selected one.
  const [thinkingGuids, setThinkingGuids] = useState(new Set());

  // Derive thinking character names from the GUID set
  const thinkingNames = useMemo(() => {
    if (thinkingGuids.size === 0) return [];
    const names = [];
    for (const char of allCharacters) {
      if (thinkingGuids.has(char.guid)) names.push(char.name);
    }
    return names;
  }, [thinkingGuids, allCharacters]);

  // Get the name of the currently selected character
  const selectedChar = useMemo(() => {
    if (!selectedGuid) return null;
    return allCharacters.find(c => c.guid === selectedGuid) || null;
  }, [selectedGuid, allCharacters]);

  const selectedCharName = selectedChar ? selectedChar.name : null;

  // Process global WS events forwarded from App.
  // These are not character-specific and must not depend on selectedGuid.
  useEffect(() => {
    if (!wsEvent) return;

    const eventGuid = wsEvent.data && wsEvent.data.guid;

    // Track thinking state for ALL characters (not just selected)
    if (wsEvent.type === 'thinks' && eventGuid) {
      setThinkingGuids((prev) => {
        const next = new Set(prev);
        next.add(eventGuid);
        return next;
      });
    }
    if ((wsEvent.type === 'history' || wsEvent.type === 'memory') && eventGuid) {
      setThinkingGuids((prev) => {
        if (!prev.has(eventGuid)) return prev;
        const next = new Set(prev);
        next.delete(eventGuid);
        return next;
      });
    }

    switch (wsEvent.type) {
      case 'connected':
        toast('Server connection established', 'success');
        break;
      case 'error':
        toast(wsEvent.data.message || 'WebSocket error', 'error');
        break;
    }
  }, [wsEvent, toast]);

  // Process character-specific WS events for the currently selected character.
  useEffect(() => {
    if (!wsEvent) return;

    const eventGuid = wsEvent.data && wsEvent.data.guid;
    const isForSelected = !eventGuid || eventGuid === selectedGuid;

    switch (wsEvent.type) {
      case 'history':
        if (isForSelected) {
          setChatEvent(wsEvent);
          setInfoReloadKey((k) => k + 1);
        }
        break;
      case 'thinks':
        if (isForSelected) {
          setChatEvent(wsEvent);
        }
        break;
      case 'relationship':
        if (isForSelected) {
          setInfoReloadKey((k) => k + 1);
        }
        break;
      case 'memory':
        if (isForSelected) {
          const memCharName = selectedCharName || 'character';
          toast(`The memory of ${memCharName} was updated`, 'success');
        }
        break;
    }
  }, [wsEvent, toast, selectedGuid, selectedCharName]);

  // Build a map of name → class color for chat highlighting and info display.
  // Uses all characters on the account + player name.
  const nameColorMap = useMemo(() => {
    const map = {};
    if (playerChar) {
      const playerColor = getClassColor(playerChar.class);
      if (playerColor) map[playerChar.name] = playerColor;
    }
    for (const char of allCharacters) {
      const color = getClassColor(char.class);
      if (color) map[char.name] = color;
    }
    return map;
  }, [playerChar, allCharacters]);

  // Determine category of the selected character: 'party', 'online', or 'offline'
  const selectedCategory = useMemo(() => {
    if (!selectedChar) return 'offline';
    if (selectedChar.is_online && partyGuids.includes(selectedChar.guid)) return 'party';
    if (selectedChar.is_online) return 'online';
    return 'offline';
  }, [selectedChar, partyGuids]);

  const handleSelect = useCallback((guid) => {
    if (guid !== selectedGuid) {
      setSelectedGuid(guid);
      // Apply faction theme based on the selected character's race
      const char = allCharacters.find(c => c.guid === guid);
      if (char) {
        applyFactionTheme(char.race);
      }
      // On mobile, switch to chat tab when selecting a character
      if (isMobile) {
        setActiveTab(TAB.CHAT);
      }
    }
  }, [selectedGuid, isMobile, allCharacters]);

  // --- Shared sub-renders ---

  // Faction icon based on selected character (or generic if none selected)
  const selectedFaction = selectedChar ? getFaction(selectedChar.race) : null;
  const factionIcon = selectedFaction ? `/images/${selectedFaction}.png` : null;

  const topBar = (
    <div class="p-3 border-bottom player-info-bar d-flex align-items-center justify-content-between">
      <div class="d-flex align-items-center gap-2">
        <span
          class="fw-bold text-decoration-underline"
          style="font-size: 1.1rem; cursor: pointer"
          onClick={onOpenAccountManager}
          title="Account Manager"
        >{account.account}</span>
        <span class="badge bg-secondary">{allCharacters.length} character{allCharacters.length !== 1 ? 's' : ''}</span>
      </div>
      {factionIcon && (
        <img
          src={factionIcon}
          alt={selectedFaction === 'horde' ? 'Horde' : 'Alliance'}
          class="faction-icon"
          width="48"
          height="48"
        />
      )}
    </div>
  );

  const renderSection = (title, chars, category) => {
    if (chars.length === 0) return null;
    return (
      <div class="mb-3">
        <div class="text-body-secondary small fw-semibold text-uppercase mb-2 px-1">{title}</div>
        <div class="d-flex flex-column gap-2">
          {chars.map((char) => {
            const isPlayer = char.is_player;
            const isInParty = category === 'party';
            return (
              <CharacterCard
                key={char.guid}
                name={char.name}
                level={char.level}
                gender={char.gender}
                race={char.race}
                cls={char.class}
                selected={selectedGuid === char.guid}
                onClick={() => handleSelect(char.guid)}
                badge={isPlayer ? 'Player' : undefined}
              />
            );
          })}
        </div>
      </div>
    );
  };

  const charactersList = (
    <div class={isMobile ? 'overflow-auto flex-grow-1 p-2' : 'border-end overflow-auto p-2'} style={isMobile ? 'min-height: 0' : 'width: 20%'}>
      {allCharacters.length === 0 && (
        <p class="text-body-secondary text-center mb-0 small">No characters on this account</p>
      )}
      {renderSection('Party', partyChars, 'party')}
      {renderSection('Online', onlineChars, 'online')}
      {renderSection('Offline', offlineChars, 'offline')}
    </div>
  );

  const chatArea = selectedGuid ? (
    <ChatView
      key={selectedGuid}
      token={token}
      selectedGuid={selectedGuid}
      nameColorMap={nameColorMap}
      charName={selectedCharName}
      thinkingNames={thinkingNames}
      playerName={playerChar ? playerChar.name : account.account}
      playerGuid={playerGuid}
      chatEvent={chatEvent}
      chatReloadKey={chatReloadKey}
      onDesync={onDesync}
      characters={allCharacters}
      messageMode={messageMode}
      onMessageModeChange={setMessageMode}
      maxHistoryCtx={maxHistoryCtx}
      charCategory={selectedCategory}
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
        {topBar}
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
      {topBar}
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
