#!/usr/bin/env python3
"""
Quest Data Explorer for AzerothCore

Extracts ALL available data about one or more quests from the database,
including resolved names for NPCs, items, gameobjects, zones, factions, etc.

Usage:
    python3 quest_data_explorer.py [quest_id ...]
    python3 quest_data_explorer.py 826 872

If no quest IDs are given, picks two sample quests with diverse objectives.
"""

import sys
import json
import pymysql

# ── Database connection ──────────────────────────────────────────────────────

def get_connection(db="acore_world"):
    return pymysql.connect(
        host="127.0.0.1",
        port=3306,
        user="acore",
        password="acore",
        database=db,
        cursorclass=pymysql.cursors.Cursor,
    )

# ── Helper: single-row dict query ────────────────────────────────────────────

def query_one(cur, sql, params=None):
    cur.execute(sql, params)
    row = cur.fetchone()
    if row is None:
        return None
    cols = [d[0] for d in cur.description]
    return dict(zip(cols, row))

# ── Helper: multi-row dict query ─────────────────────────────────────────────

def query_all(cur, sql, params=None):
    cur.execute(sql, params)
    rows = cur.fetchall()
    if not rows:
        return []
    cols = [d[0] for d in cur.description]
    return [dict(zip(cols, r)) for r in rows]

# ── Resolve creature name ────────────────────────────────────────────────────

def creature_name(cur, entry):
    if not entry or entry <= 0:
        return None
    r = query_one(cur, "SELECT entry, name, subname FROM acore_world.creature_template WHERE entry = %s", (entry,))
    return r if r else {"entry": entry, "name": f"<unknown creature {entry}>"}

# ── Resolve gameobject name ──────────────────────────────────────────────────

def gameobject_name(cur, entry):
    if not entry or entry <= 0:
        return None
    # GO entries are stored as negative in RequiredNpcOrGo
    actual = abs(entry)
    r = query_one(cur, "SELECT entry, name, type FROM acore_world.gameobject_template WHERE entry = %s", (actual,))
    return r if r else {"entry": actual, "name": f"<unknown gameobject {actual}>"}

# ── Resolve item name ───────────────────────────────────────────────────────

def item_name(cur, entry):
    if not entry or entry <= 0:
        return None
    r = query_one(cur, "SELECT entry, name, Quality, class, subclass FROM acore_world.item_template WHERE entry = %s", (entry,))
    return r if r else {"entry": entry, "name": f"<unknown item {entry}>"}

# ── Resolve faction name ─────────────────────────────────────────────────────

def faction_name(cur, id):
    if not id or id <= 0:
        return None
    r = query_one(cur, "SELECT ID, Name_Lang_enUS FROM acore_world.faction_dbc WHERE ID = %s", (id,))
    return r if r else {"ID": id, "Name_Lang_enUS": f"<unknown faction {id}>"}

# ── Resolve skill name ───────────────────────────────────────────────────────

def skill_name(cur, id):
    if not id or id <= 0:
        return None
    r = query_one(cur, "SELECT ID, Name_Lang_enUS FROM acore_world.skillline_dbc WHERE ID = %s", (id,))
    return r if r else {"ID": id, "Name_Lang_enUS": f"<unknown skill {id}>"}

# ── Resolve zone name ────────────────────────────────────────────────────────

def zone_name(cur, id):
    if not id:
        return None
    r = query_one(cur, "SELECT ID, AreaName_Lang_enUS FROM acore_world.area_table_dbc WHERE ID = %s", (id,))
    return r if r else None

# ── Resolve quest sort name ──────────────────────────────────────────────────

def quest_sort_name(cur, sort_id):
    if not sort_id or sort_id == 0:
        return None
    # Negative sort IDs are zone IDs
    if sort_id > 0:
        r = query_one(cur, "SELECT ID, SortName_Lang_enUS FROM acore_world.questsort_dbc WHERE ID = %s", (sort_id,))
        return r if r else {"ID": sort_id, "SortName_Lang_enUS": f"<unknown sort {sort_id}>"}
    else:
        # It's a zone ID (negated)
        z = zone_name(cur, abs(sort_id))
        if z:
            return {"zone_id": abs(sort_id), "zone_name": z.get("AreaName_Lang_enUS", "?")}
        return {"zone_id": abs(sort_id), "zone_name": f"<unknown zone {abs(sort_id)}>"}

# ── Resolve spell name ───────────────────────────────────────────────────────

def spell_name(cur, id):
    if not id or id <= 0:
        return None
    r = query_one(cur, "SELECT ID, SpellName_Lang_enUS FROM acore_world.spell_dbc WHERE ID = %s", (id,))
    return r if r else {"ID": id, "SpellName_Lang_enUS": f"<unknown spell {id}>"}

# ── Main extraction ──────────────────────────────────────────────────────────

def extract_quest(quest_id):
    conn = get_connection("acore_world")
    cur = conn.cursor()

    result = {"quest_id": quest_id}

    # ── 1. quest_template (the main table) ──────────────────────────────────
    qt = query_one(cur, "SELECT * FROM acore_world.quest_template WHERE ID = %s", (quest_id,))
    if qt is None:
        print(f"Quest {quest_id} not found in quest_template!")
        conn.close()
        return None

    result["quest_template"] = qt

    # Resolve QuestSortID
    if qt.get("QuestSortID"):
        result["quest_sort"] = quest_sort_name(cur, qt["QuestSortID"])

    # Resolve RequiredNpcOrGo (1-4): positive = creature, negative = gameobject
    for i in range(1, 5):
        col = f"RequiredNpcOrGo{i}"
        val = qt.get(col, 0)
        if val and val > 0:
            result[f"objective_creature_{i}"] = creature_name(cur, val)
        elif val and val < 0:
            result[f"objective_gameobject_{i}"] = gameobject_name(cur, val)

    # Resolve RequiredItemId (1-6)
    for i in range(1, 7):
        col = f"RequiredItemId{i}"
        val = qt.get(col, 0)
        if val and val > 0:
            result[f"objective_item_{i}"] = item_name(cur, val)

    # Resolve RewardItem (1-4)
    for i in range(1, 5):
        col = f"RewardItem{i}"
        val = qt.get(col, 0)
        if val and val > 0:
            result[f"reward_item_{i}"] = item_name(cur, val)

    # Resolve RewardChoiceItemID (1-6)
    for i in range(1, 7):
        col = f"RewardChoiceItemID{i}"
        val = qt.get(col, 0)
        if val and val > 0:
            result[f"reward_choice_item_{i}"] = item_name(cur, val)

    # Resolve ItemDrop (1-4)
    for i in range(1, 5):
        col = f"ItemDrop{i}"
        val = qt.get(col, 0)
        if val and val > 0:
            result[f"item_drop_{i}"] = item_name(cur, val)

    # Resolve StartItem
    if qt.get("StartItem", 0) > 0:
        result["start_item"] = item_name(cur, qt["StartItem"])

    # Resolve RewardFactionID (1-5)
    for i in range(1, 6):
        col = f"RewardFactionID{i}"
        val = qt.get(col, 0)
        if val and val > 0:
            result[f"reward_faction_{i}"] = faction_name(cur, val)

    # Resolve RequiredFactionId (1-2)
    for i in range(1, 3):
        col = f"RequiredFactionId{i}"
        val = qt.get(col, 0)
        if val and val > 0:
            result[f"required_faction_{i}"] = faction_name(cur, val)

    # ── 2. quest_template_addon ─────────────────────────────────────────────
    qta = query_one(cur, "SELECT * FROM acore_world.quest_template_addon WHERE ID = %s", (quest_id,))
    if qta:
        result["quest_template_addon"] = qta

        if qta.get("RequiredSkillID", 0) > 0:
            result["required_skill"] = skill_name(cur, qta["RequiredSkillID"])
        if qta.get("SourceSpellID", 0) > 0:
            result["source_spell"] = spell_name(cur, qta["SourceSpellID"])
        if qta.get("PrevQuestID", 0) != 0:
            prev = query_one(cur, "SELECT ID, LogTitle FROM acore_world.quest_template WHERE ID = %s", (abs(qta["PrevQuestID"]),))
            result["prev_quest"] = prev
        if qta.get("NextQuestID", 0) != 0:
            nxt = query_one(cur, "SELECT ID, LogTitle FROM acore_world.quest_template WHERE ID = %s", (qta["NextQuestID"],))
            result["next_quest"] = nxt

    # ── 3. quest_details (emotes on offer) ──────────────────────────────────
    qd = query_one(cur, "SELECT * FROM acore_world.quest_details WHERE ID = %s", (quest_id,))
    if qd:
        result["quest_details"] = qd

    # ── 4. quest_request_items (emotes + completion text on "incomplete" turn-in) ──
    qri = query_one(cur, "SELECT * FROM acore_world.quest_request_items WHERE ID = %s", (quest_id,))
    if qri:
        result["quest_request_items"] = qri

    # ── 5. quest_offer_reward (emotes + reward text on "complete" turn-in) ──
    qor = query_one(cur, "SELECT * FROM acore_world.quest_offer_reward WHERE ID = %s", (quest_id,))
    if qor:
        result["quest_offer_reward"] = qor

    # ── 6. quest_greeting (greeting text when NPC offers the quest) ─────────
    qg = query_all(cur, "SELECT * FROM acore_world.quest_greeting WHERE ID = %s", (quest_id,))
    if qg:
        result["quest_greeting"] = qg

    # ── 7. creature_queststarter (who gives this quest) ─────────────────────
    cqs = query_all(cur, "SELECT id FROM acore_world.creature_queststarter WHERE quest = %s", (quest_id,))
    if cqs:
        starters = []
        for row in cqs:
            info = creature_name(cur, row["id"])
            if info:
                starters.append(info)
        result["creature_quest_starters"] = starters

    # ── 8. creature_questender (who completes this quest) ───────────────────
    cqe = query_all(cur, "SELECT id FROM acore_world.creature_questender WHERE quest = %s", (quest_id,))
    if cqe:
        enders = []
        for row in cqe:
            info = creature_name(cur, row["id"])
            if info:
                enders.append(info)
        result["creature_quest_enders"] = enders

    # ── 9. gameobject_queststarter ──────────────────────────────────────────
    gqs = query_all(cur, "SELECT id FROM acore_world.gameobject_queststarter WHERE quest = %s", (quest_id,))
    if gqs:
        starters = []
        for row in gqs:
            info = gameobject_name(cur, row["id"])
            if info:
                starters.append(info)
        result["gameobject_quest_starters"] = starters

    # ── 10. gameobject_questender ───────────────────────────────────────────
    gqe = query_all(cur, "SELECT id FROM acore_world.gameobject_questender WHERE quest = %s", (quest_id,))
    if gqe:
        enders = []
        for row in gqe:
            info = gameobject_name(cur, row["id"])
            if info:
                enders.append(info)
        result["gameobject_quest_enders"] = enders

    # ── 11. quest_poi + quest_poi_points (map markers for objectives) ───────
    pois = query_all(cur, "SELECT * FROM acore_world.quest_poi WHERE QuestID = %s ORDER BY id", (quest_id,))
    if pois:
        poi_data = []
        for poi in pois:
            points = query_all(cur,
                "SELECT * FROM acore_world.quest_poi_points WHERE QuestID = %s AND Idx1 = %s ORDER BY Idx2",
                (quest_id, poi["id"]))
            poi["points"] = points
            # Resolve map name
            map_id = poi.get("MapID", 0)
            if map_id:
                map_info = query_one(cur, "SELECT ID, MapName_Lang_enUS FROM acore_world.map_dbc WHERE ID = %s", (map_id,))
                if map_info:
                    poi["map_name"] = map_info.get("MapName_Lang_enUS", "")
            poi_data.append(poi)
        result["quest_poi"] = poi_data

    # ── 12. quest_money_reward ──────────────────────────────────────────────
    qmr = query_one(cur, "SELECT * FROM acore_world.quest_money_reward WHERE Level = %s", (qt.get("RewardMoneyDifficulty", 0),))
    if qmr:
        result["quest_money_reward"] = qmr

    # ── 13. questxp_dbc ─────────────────────────────────────────────────────
    qxp = query_one(cur, "SELECT * FROM acore_world.questxp_dbc WHERE ID = %s", (qt.get("RewardXPDifficulty", 0),))
    if qxp:
        result["quest_xp_dbc"] = qxp

    # ── 14. quest_mail_sender ───────────────────────────────────────────────
    qms = query_one(cur, "SELECT * FROM acore_world.quest_mail_sender WHERE QuestId = %s", (quest_id,))
    if qms:
        result["quest_mail_sender"] = qms

    # ── 15. quest_template_locale (enUS only) ───────────────────────────────
    qtl = query_all(cur, "SELECT * FROM acore_world.quest_template_locale WHERE ID = %s AND locale = 'enUS'", (quest_id,))
    if qtl:
        result["quest_template_locale"] = qtl

    # ── 16. questfactionreward_dbc ──────────────────────────────────────────
    # This is used for faction reward multipliers
    for i in range(1, 6):
        override = qt.get(f"RewardFactionOverride{i}", 0)
        if override != 0:
            fval = qt.get(f"RewardFactionValue{i}", 0)
            result[f"faction_reward_detail_{i}"] = {
                "faction_id": qt.get(f"RewardFactionID{i}", 0),
                "base_value": fval,
                "override": override,
            }

    # ── 17. game_event_creature_quest (seasonal events) ─────────────────────
    gecq = query_all(cur, "SELECT * FROM acore_world.game_event_creature_quest WHERE quest = %s", (quest_id,))
    if gecq:
        result["game_event_creature_quest"] = gecq

    # ── 18. game_event_gameobject_quest ─────────────────────────────────────
    gegq = query_all(cur, "SELECT * FROM acore_world.game_event_gameobject_quest WHERE quest = %s", (quest_id,))
    if gegq:
        result["game_event_gameobject_quest"] = gegq

    # ── 19. game_event_quest_condition ──────────────────────────────────────
    geqc = query_all(cur, "SELECT * FROM acore_world.game_event_quest_condition WHERE quest = %s", (quest_id,))
    if geqc:
        result["game_event_quest_condition"] = geqc

    # ── 20. player_factionchange_quests ─────────────────────────────────────
    pfcq = query_all(cur, "SELECT * FROM acore_world.player_factionchange_quests WHERE alliance_id = %s OR horde_id = %s", (quest_id, quest_id))
    if pfcq:
        result["player_factionchange_quests"] = pfcq

    # ── 21. pool_quest ──────────────────────────────────────────────────────
    pq = query_all(cur, "SELECT * FROM acore_world.pool_quest WHERE entry = %s", (quest_id,))
    if pq:
        result["pool_quest"] = pq

    # ── 22. creature_questitem (creatures that drop quest items for this quest) ──
    # This table maps CreatureEntry -> ItemId, not by QuestID.
    # We need to find which creatures drop the RequiredItemIds for this quest.
    quest_item_ids = []
    for i in range(1, 7):
        iid = qt.get(f"RequiredItemId{i}", 0)
        if iid > 0:
            quest_item_ids.append(iid)
    if quest_item_ids:
        cqi_rows = []
        for iid in quest_item_ids:
            rows = query_all(cur, "SELECT * FROM acore_world.creature_questitem WHERE ItemId = %s", (iid,))
            for row in rows:
                row["item_info"] = item_name(cur, iid)
                row["creature_info"] = creature_name(cur, row.get("CreatureEntry", 0))
                cqi_rows.append(row)
        if cqi_rows:
            result["creature_questitem"] = cqi_rows

    # ── 23. gameobject_questitem ────────────────────────────────────────────
    if quest_item_ids:
        goqi_rows = []
        for iid in quest_item_ids:
            rows = query_all(cur, "SELECT * FROM acore_world.gameobject_questitem WHERE ItemId = %s", (iid,))
            for row in rows:
                row["item_info"] = item_name(cur, iid)
                row["gameobject_info"] = gameobject_name(cur, row.get("GameObjectEntry", 0))
                goqi_rows.append(row)
        if goqi_rows:
            result["gameobject_questitem"] = goqi_rows

    # ── 24. Character quest status (from acore_characters) ──────────────────
    conn2 = get_connection("acore_characters")
    cur2 = conn2.cursor()

    cqs_status = query_all(cur2,
        "SELECT guid, quest, status, explored, timer, mobcount1, mobcount2, mobcount3, mobcount4, itemcount1, itemcount2, itemcount3, itemcount4, itemcount5, itemcount6, playercount FROM character_queststatus WHERE quest = %s LIMIT 5",
        (quest_id,))
    if cqs_status:
        result["character_queststatus_samples"] = cqs_status

    # Rewarded status
    cqr = query_all(cur2,
        "SELECT * FROM character_queststatus_rewarded WHERE quest = %s LIMIT 5",
        (quest_id,))
    if cqr:
        result["character_queststatus_rewarded_samples"] = cqr

    cur2.close()
    conn2.close()

    cur.close()
    conn.close()

    return result


# ── Pretty printer ───────────────────────────────────────────────────────────

def print_quest_report(data):
    qid = data["quest_id"]
    qt = data["quest_template"]

    print("=" * 80)
    print(f"  QUEST REPORT: {qt.get('LogTitle', '?')} (ID: {qid})")
    print("=" * 80)

    # ── Basic info ───────────────────────────────────────────────────────────
    print(f"\n── Basic Info ──")
    print(f"  Title:              {qt.get('LogTitle', '')}")
    print(f"  Quest Type:         {qt.get('QuestType', '')}")
    print(f"  Quest Level:        {qt.get('QuestLevel', '')}")
    print(f"  Min Level:          {qt.get('MinLevel', '')}")
    print(f"  Quest Info ID:      {qt.get('QuestInfoID', '')}  (1=Elite, 21=Raid, 41=PvP, 62=Dungeon, 81=Raid10, 82=Raid25, 83=Raid10H, 85=Raid25H)")
    print(f"  Suggested Group:    {qt.get('SuggestedGroupNum', '')}")
    print(f"  Flags:              {qt.get('Flags', 0):#010x}")
    print(f"  Allowable Races:    {qt.get('AllowableRaces', 0)}  (bitmask: 1=Human, 2=Orc, 4=Dwarf, 8=NElf, 16=Undead, 32=Tauren, 64=Gnome, 128=Troll)")
    print(f"  Time Allowed:       {qt.get('TimeAllowed', 0)}  (0 = no timer)")

    # Sort / Zone
    qs = data.get("quest_sort")
    if qs:
        print(f"  Zone/Sort:          {qs}")

    # ── Text fields ──────────────────────────────────────────────────────────
    print(f"\n── Text Fields ──")
    desc = qt.get('LogDescription', '')
    if desc:
        print(f"  Log Description:    {desc}")
    desc_full = qt.get('QuestDescription', '')
    if desc_full:
        print(f"  Quest Description:  {desc_full[:200]}{'...' if len(desc_full) > 200 else ''}")
    area_desc = qt.get('AreaDescription', '')
    if area_desc:
        print(f"  Area Description:   {area_desc}")
    completion_log = qt.get('QuestCompletionLog', '')
    if completion_log:
        print(f"  Completion Log:     {completion_log}")

    # ── Objectives (kill/interact) ───────────────────────────────────────────
    print(f"\n── Kill/Interact Objectives (RequiredNpcOrGo) ──")
    has_obj = False
    for i in range(1, 5):
        npc_col = f"RequiredNpcOrGo{i}"
        cnt_col = f"RequiredNpcOrGoCount{i}"
        txt_col = f"ObjectiveText{i}"
        val = qt.get(npc_col, 0)
        cnt = qt.get(cnt_col, 0)
        txt = qt.get(txt_col, '')
        if val != 0:
            has_obj = True
            if val > 0:
                info = data.get(f"objective_creature_{i}")
                name = info["name"] if info else f"<creature {val}>"
                sub = f" ({info['subname']})" if info and info.get("subname") else ""
                print(f"  [{i}] Kill/Credit: {name}{sub} (entry={val}) x{cnt}  ObjectiveText: '{txt}'")
            else:
                info = data.get(f"objective_gameobject_{i}")
                name = info["name"] if info else f"<gameobject {abs(val)}>"
                print(f"  [{i}] Interact: {name} (entry={abs(val)}) x{cnt}  ObjectiveText: '{txt}'")
    if not has_obj:
        print("  (none)")

    # ── Item objectives ──────────────────────────────────────────────────────
    print(f"\n── Item Objectives (RequiredItem) ──")
    has_item_obj = False
    for i in range(1, 7):
        item_col = f"RequiredItemId{i}"
        cnt_col = f"RequiredItemCount{i}"
        val = qt.get(item_col, 0)
        cnt = qt.get(cnt_col, 0)
        if val > 0:
            has_item_obj = True
            info = data.get(f"objective_item_{i}")
            name = info["name"] if info else f"<item {val}>"
            quality = info.get("Quality", "?") if info else "?"
            print(f"  [{i}] Collect: {name} (entry={val}, quality={quality}) x{cnt}")
    if not has_item_obj:
        print("  (none)")

    # ── Player kill objective ────────────────────────────────────────────────
    pk = qt.get("RequiredPlayerKills", 0)
    if pk:
        print(f"\n── Player Kill Objective: {pk} players ──")

    # ── Source/Start item ────────────────────────────────────────────────────
    si = qt.get("StartItem", 0)
    if si > 0:
        info = data.get("start_item")
        name = info["name"] if info else f"<item {si}>"
        print(f"\n  Start Item: {name} (entry={si})")

    # ── Item drops (source items that drop quest items) ──────────────────────
    print(f"\n── Item Drops (source items for quest items) ──")
    has_drops = False
    for i in range(1, 5):
        col = f"ItemDrop{i}"
        qty_col = f"ItemDropQuantity{i}"
        val = qt.get(col, 0)
        qty = qt.get(qty_col, 0)
        if val > 0:
            has_drops = True
            info = data.get(f"item_drop_{i}")
            name = info["name"] if info else f"<item {val}>"
            print(f"  [{i}] {name} (entry={val}) drops x{qty}")
    if not has_drops:
        print("  (none)")

    # ── Quest Givers ─────────────────────────────────────────────────────────
    cqs = data.get("creature_quest_starters", [])
    if cqs:
        print(f"\n── Quest Givers (Creatures) ──")
        for s in cqs:
            sub = f" ({s['subname']})" if s.get("subname") else ""
            print(f"  {s['name']}{sub} (entry={s['entry']})")

    gqs = data.get("gameobject_quest_starters", [])
    if gqs:
        print(f"\n── Quest Givers (GameObjects) ──")
        for s in gqs:
            print(f"  {s['name']} (entry={s['entry']}, type={s.get('type', '?')})")

    # ── Quest Enders ─────────────────────────────────────────────────────────
    cqe = data.get("creature_quest_enders", [])
    if cqe:
        print(f"\n── Quest Enders (Creatures) ──")
        for s in cqe:
            sub = f" ({s['subname']})" if s.get("subname") else ""
            print(f"  {s['name']}{sub} (entry={s['entry']})")

    gqe = data.get("gameobject_quest_enders", [])
    if gqe:
        print(f"\n── Quest Enders (GameObjects) ──")
        for s in gqe:
            print(f"  {s['name']} (entry={s['entry']}, type={s.get('type', '?')})")

    # ── Request Items text ───────────────────────────────────────────────────
    qri = data.get("quest_request_items")
    if qri:
        print(f"\n── Request Items (incomplete turn-in dialogue) ──")
        ct = qri.get("CompletionText", "")
        if ct:
            print(f"  CompletionText: {ct[:200]}{'...' if len(str(ct)) > 200 else ''}")
        print(f"  EmoteOnComplete:   {qri.get('EmoteOnComplete', 0)}")
        print(f"  EmoteOnIncomplete: {qri.get('EmoteOnIncomplete', 0)}")

    # ── Offer Reward text ────────────────────────────────────────────────────
    qor = data.get("quest_offer_reward")
    if qor:
        print(f"\n── Offer Reward (complete turn-in dialogue) ──")
        rt = qor.get("RewardText", "")
        if rt:
            print(f"  RewardText: {rt[:300]}{'...' if len(str(rt)) > 300 else ''}")
        for i in range(1, 5):
            emote = qor.get(f"Emote{i}", 0)
            delay = qor.get(f"EmoteDelay{i}", 0)
            if emote:
                print(f"  Emote{i}: {emote} (delay={delay}ms)")

    # ── Quest Greeting ───────────────────────────────────────────────────────
    qg = data.get("quest_greeting")
    if qg:
        print(f"\n── Quest Greeting ──")
        for g in qg:
            print(f"  Type={g.get('type', '?')}, EmoteType={g.get('GreetEmoteType', 0)}, Delay={g.get('GreetEmoteDelay', 0)}")
            gt = g.get("Greeting", "")
            if gt:
                print(f"  Greeting: {gt[:200]}{'...' if len(str(gt)) > 200 else ''}")

    # ── Rewards ──────────────────────────────────────────────────────────────
    print(f"\n── Rewards ──")
    money = qt.get("RewardMoney", 0)
    if money:
        gold = money / 10000
        print(f"  Money: {money} copper ({gold:.2f} gold)")
    xp_diff = qt.get("RewardXPDifficulty", 0)
    if xp_diff:
        print(f"  XP Difficulty: {xp_diff}")
        qxp = data.get("quest_xp_dbc")
        if qxp:
            print(f"  XP Table: {qxp}")
    honor = qt.get("RewardHonor", 0)
    if honor:
        print(f"  Honor: {honor}")
    spell = qt.get("RewardSpell", 0)
    if spell:
        sp = data.get("source_spell")  # might be different
        print(f"  Reward Spell Cast: {spell}")
    disp_spell = qt.get("RewardDisplaySpell", 0)
    if disp_spell:
        print(f"  Reward Display Spell: {disp_spell}")
    title = qt.get("RewardTitle", 0)
    if title:
        print(f"  Reward Title ID: {title}")
    talents = qt.get("RewardTalents", 0)
    if talents:
        print(f"  Reward Talents: {talents}")
    arena = qt.get("RewardArenaPoints", 0)
    if arena:
        print(f"  Reward Arena Points: {arena}")
    next_q = qt.get("RewardNextQuest", 0)
    if next_q:
        print(f"  Next Quest in Chain: {next_q}")

    # Fixed reward items
    for i in range(1, 5):
        col = f"RewardItem{i}"
        cnt_col = f"RewardAmount{i}"
        val = qt.get(col, 0)
        cnt = qt.get(cnt_col, 0)
        if val > 0:
            info = data.get(f"reward_item_{i}")
            name = info["name"] if info else f"<item {val}>"
            print(f"  Reward Item {i}: {name} (entry={val}) x{cnt}")

    # Choice reward items
    for i in range(1, 7):
        col = f"RewardChoiceItemID{i}"
        cnt_col = f"RewardChoiceItemQuantity{i}"
        val = qt.get(col, 0)
        cnt = qt.get(cnt_col, 0)
        if val > 0:
            info = data.get(f"reward_choice_item_{i}")
            name = info["name"] if info else f"<item {val}>"
            print(f"  Choice Item {i}: {name} (entry={val}) x{cnt}")

    # Faction rewards
    for i in range(1, 6):
        fid = qt.get(f"RewardFactionID{i}", 0)
        if fid > 0:
            fval = qt.get(f"RewardFactionValue{i}", 0)
            fov = qt.get(f"RewardFactionOverride{i}", 0)
            info = data.get(f"reward_faction_{i}")
            fname = info.get("Name_Lang_enUS", "?") if info else "?"
            print(f"  Faction Reward {i}: {fname} (id={fid}) value={fval} override={fov}")

    # ── Addon data ───────────────────────────────────────────────────────────
    qta = data.get("quest_template_addon")
    if qta:
        print(f"\n── Quest Template Addon ──")
        print(f"  Max Level:          {qta.get('MaxLevel', 0)}")
        print(f"  Allowable Classes:  {qta.get('AllowableClasses', 0)}  (bitmask)")
        print(f"  Prev Quest ID:      {qta.get('PrevQuestID', 0)}")
        print(f"  Next Quest ID:      {qta.get('NextQuestID', 0)}")
        print(f"  Exclusive Group:    {qta.get('ExclusiveGroup', 0)}")
        print(f"  Breadcrumb For:     {qta.get('BreadcrumbForQuestId', 0)}")
        print(f"  Required Skill:     {data.get('required_skill', {}).get('Name_Lang_enUS', qta.get('RequiredSkillID', 0))} (id={qta.get('RequiredSkillID', 0)}, points={qta.get('RequiredSkillPoints', 0)})")
        print(f"  Source Spell:       {data.get('source_spell', {}).get('SpellName_Lang_enUS', qta.get('SourceSpellID', 0))} (id={qta.get('SourceSpellID', 0)})")
        print(f"  RequiredMinRepFaction: {qta.get('RequiredMinRepFaction', 0)}")
        print(f"  RequiredMinRepValue:   {qta.get('RequiredMinRepValue', 0)}")
        print(f"  RequiredMaxRepFaction: {qta.get('RequiredMaxRepFaction', 0)}")
        print(f"  RequiredMaxRepValue:   {qta.get('RequiredMaxRepValue', 0)}")
        print(f"  ProvidedItemCount:     {qta.get('ProvidedItemCount', 0)}")
        print(f"  SpecialFlags:      {qta.get('SpecialFlags', 0)}  (1=Repeatable, 2=Exploration/Event, 4=AutoAccept, 8=DF, 16=Monthly, 32=Cast, 64=NoRepSpillover)")
        print(f"  Reward Mail Template:  {qta.get('RewardMailTemplateID', 0)}")
        print(f"  Reward Mail Delay:     {qta.get('RewardMailDelay', 0)}")

        pq = data.get("prev_quest")
        if pq:
            print(f"  Prev Quest Resolved:   {pq.get('LogTitle', '?')} (ID={pq.get('ID', '?')})")
        nq = data.get("next_quest")
        if nq:
            print(f"  Next Quest Resolved:   {nq.get('LogTitle', '?')} (ID={nq.get('ID', '?')})")

    # ── POI data ─────────────────────────────────────────────────────────────
    pois = data.get("quest_poi")
    if pois:
        print(f"\n── Quest POI (Map Markers) ──")
        for poi in pois:
            obj_idx = poi.get("ObjectiveIndex", 0)
            map_id = poi.get("MapID", 0)
            map_name = poi.get("map_name", "?")
            floor = poi.get("Floor", 0)
            priority = poi.get("Priority", 0)
            flags = poi.get("Flags", 0)
            print(f"  ObjectiveIndex={obj_idx}, Map={map_id} ({map_name}), Floor={floor}, Priority={priority}, Flags={flags}")
            points = poi.get("points", [])
            if points:
                coords = [(p.get("X", 0), p.get("Y", 0)) for p in points]
                # Show just a summary
                print(f"    Points: {len(coords)} vertices")
                for c in coords[:5]:
                    print(f"      ({c[0]}, {c[1]})")
                if len(coords) > 5:
                    print(f"      ... and {len(coords) - 5} more")

    # ── Creature/GO quest items ──────────────────────────────────────────────
    cqi = data.get("creature_questitem")
    if cqi:
        print(f"\n── Creature Quest Items (drop from specific creatures) ──")
        for row in cqi:
            info = row.get("item_info", {})
            name = info.get("name", "?") if info else "?"
            print(f"  CreatureEntry={row.get('CreatureEntry', '?')}, ItemId={row.get('ItemId', '?')} ({name}), idx={row.get('Idx', '?')}")

    goqi = data.get("gameobject_questitem")
    if goqi:
        print(f"\n── GameObject Quest Items (from specific GOs) ──")
        for row in goqi:
            info = row.get("item_info", {})
            name = info.get("name", "?") if info else "?"
            print(f"  GameObjectEntry={row.get('GameObjectEntry', '?')}, ItemId={row.get('ItemId', '?')} ({name}), idx={row.get('Idx', '?')}")

    # ── Seasonal/Events ─────────────────────────────────────────────────────
    gecq = data.get("game_event_creature_quest")
    if gecq:
        print(f"\n── Game Event Creature Quest ──")
        for row in gecq:
            print(f"  EventEntry={row.get('eventEntry', '?')}, Creature={row.get('id', '?')}, Quest={row.get('quest', '?')}")

    # ── Character quest status samples ───────────────────────────────────────
    cqs_samples = data.get("character_queststatus_samples")
    if cqs_samples:
        print(f"\n── Character Quest Status (sample of active/in-progress) ──")
        for row in cqs_samples:
            status = row.get("status", 0)
            status_str = {0: "NONE", 1: "COMPLETE", 3: "INCOMPLETE", 5: "FAILED", 6: "REWARDED"}.get(status, f"?{status}")
            print(f"  guid={row.get('guid', '?')}, status={status_str}, explored={row.get('explored', 0)}")
            mc = [row.get(f"mobcount{i}", 0) for i in range(1, 5)]
            ic = [row.get(f"itemcount{i}", 0) for i in range(1, 7)]
            if any(m != 0 for m in mc):
                print(f"    mob counts: {mc}")
            if any(i != 0 for i in ic):
                print(f"    item counts: {ic}")
            pc = row.get("playercount", 0)
            if pc:
                print(f"    player kills: {pc}")

    cqr_samples = data.get("character_queststatus_rewarded_samples")
    if cqr_samples:
        print(f"\n── Character Quest Status Rewarded (sample) ──")
        for row in cqr_samples:
            print(f"  guid={row.get('guid', '?')}, quest={row.get('quest', '?')}")

    # ── Summary of what the Quest C++ class exposes ──────────────────────────
    print(f"\n{'=' * 80}")
    print(f"  SUMMARY: Quest C++ API methods available for quest ID {qid}")
    print(f"{'=' * 80}")
    print(f"""
  From Quest class (src/server/game/Quests/QuestDef.h):

  Basic:
    GetQuestId()           = {qid}
    GetQuestMethod()       = {qt.get('QuestType', '?')}
    GetZoneOrSort()        = {qt.get('QuestSortID', '?')}
    GetMinLevel()          = {qt.get('MinLevel', '?')}
    GetMaxLevel()          = {qta.get('MaxLevel', '?') if qta else '?'}
    GetQuestLevel()        = {qt.get('QuestLevel', '?')}
    GetType()              = {qt.get('QuestInfoID', '?')}
    GetRequiredClasses()   = {qta.get('AllowableClasses', '?') if qta else '?'}
    GetAllowableRaces()    = {qt.get('AllowableRaces', '?')}
    GetSuggestedPlayers()  = {qt.get('SuggestedGroupNum', '?')}
    GetTimeAllowed()       = {qt.get('TimeAllowed', '?')}
    GetFlags()             = {qt.get('Flags', 0):#010x}
    IsRepeatable()         = {bool(qta.get('SpecialFlags', 0) & 1) if qta else False}
    IsDaily()              = {bool(qt.get('Flags', 0) & 0x1000)}
    IsWeekly()             = {bool(qt.get('Flags', 0) & 0x8000)}
    IsAutoComplete()       = {bool(qt.get('Flags', 0) & 0x10000)}

  Text:
    GetTitle()             = "{qt.get('LogTitle', '')}"
    GetDetails()           = "{str(qt.get('QuestDescription', ''))[:60]}..."
    GetObjectives()        = "{qt.get('LogDescription', '')}"
    GetOfferRewardText()   = "{str(qor.get('RewardText', ''))[:60] if qor else ''}..."
    GetRequestItemsText()  = "{str(qri.get('CompletionText', ''))[:60] if qri else ''}..."
    GetAreaDescription()   = "{qt.get('AreaDescription', '')}"
    GetCompletedText()     = "{qt.get('QuestCompletionLog', '')}"
    ObjectiveText[1..4]    = {["'" + str(qt.get(f'ObjectiveText{i}', '')) + "'" for i in range(1, 5)]}

  Objectives:
    RequiredNpcOrGo[1..4]     = {[qt.get(f'RequiredNpcOrGo{i}', 0) for i in range(1, 5)]}
    RequiredNpcOrGoCount[1..4]= {[qt.get(f'RequiredNpcOrGoCount{i}', 0) for i in range(1, 5)]}
    RequiredItemId[1..6]      = {[qt.get(f'RequiredItemId{i}', 0) for i in range(1, 7)]}
    RequiredItemCount[1..6]   = {[qt.get(f'RequiredItemCount{i}', 0) for i in range(1, 7)]}
    GetReqItemsCount()        = number of item objectives with non-zero entries
    GetReqCreatureOrGOcount() = number of creature/GO objectives with non-zero entries
    GetPlayersSlain()         = {qt.get('RequiredPlayerKills', 0)}

  Rewards:
    GetRewOrReqMoney()     = {qt.get('RewardMoney', 0)}
    GetRewHonorAddition()  = {qt.get('RewardHonor', 0)}
    GetRewSpell()          = {qt.get('RewardDisplaySpell', 0)}
    GetRewSpellCast()      = {qt.get('RewardSpell', 0)}
    GetXPId()              = {qt.get('RewardXPDifficulty', 0)}
    GetNextQuestInChain()  = {qt.get('RewardNextQuest', 0)}
    GetCharTitleId()       = {qt.get('RewardTitle', 0)}
    GetBonusTalents()      = {qt.get('RewardTalents', 0)}
    GetSrcItemId()         = {qt.get('StartItem', 0)}
    GetSrcItemCount()      = (from addon: {qta.get('ProvidedItemCount', 0) if qta else 0})
    RewardItemId[1..4]     = {[qt.get(f'RewardItem{i}', 0) for i in range(1, 5)]}
    RewardItemIdCount[1..4]= {[qt.get(f'RewardAmount{i}', 0) for i in range(1, 5)]}
    RewardChoiceItemId[1..6]    = {[qt.get(f'RewardChoiceItemID{i}', 0) for i in range(1, 7)]}
    RewardChoiceItemCount[1..6] = {[qt.get(f'RewardChoiceItemQuantity{i}', 0) for i in range(1, 7)]}
    RewardFactionId[1..5]       = {[qt.get(f'RewardFactionID{i}', 0) for i in range(1, 6)]}
    RewardFactionValueId[1..5]  = {[qt.get(f'RewardFactionValue{i}', 0) for i in range(1, 6)]}
    RewardFactionValueIdOverride[1..5] = {[qt.get(f'RewardFactionOverride{i}', 0) for i in range(1, 6)]}

  Chain:
    GetPrevQuestId()       = {qta.get('PrevQuestID', 0) if qta else 0}
    GetNextQuestId()       = {qta.get('NextQuestID', 0) if qta else 0}
    GetExclusiveGroup()    = {qta.get('ExclusiveGroup', 0) if qta else 0}
    GetBreadcrumbForQuestId() = {qta.get('BreadcrumbForQuestId', 0) if qta else 0}

  POI:
    GetPOIContinent()      = {qt.get('POIContinent', 0)}
    GetPOIx()              = {qt.get('POIx', 0)}
    GetPOIy()              = {qt.get('POIy', 0)}
    GetPointOpt()          = {qt.get('POIPriority', 0)}

  Emotes:
    GetIncompleteEmote()   = {qri.get('EmoteOnIncomplete', 0) if qri else 0}
    GetCompleteEmote()     = {qri.get('EmoteOnComplete', 0) if qri else 0}
    DetailsEmote[1..4]     = {[data.get('quest_details', {}).get(f'Emote{i}', 0) for i in range(1, 5)] if data.get('quest_details') else [0]*4}
    DetailsEmoteDelay[1..4]= {[data.get('quest_details', {}).get(f'EmoteDelay{i}', 0) for i in range(1, 5)] if data.get('quest_details') else [0]*4}
    OfferRewardEmote[1..4] = {[qor.get(f'Emote{i}', 0) for i in range(1, 5)] if qor else [0]*4}
    OfferRewardEmoteDelay[1..4] = {[qor.get(f'EmoteDelay{i}', 0) for i in range(1, 5)] if qor else [0]*4}

  Additional (from quest_template_addon):
    GetRequiredSkill()         = {qta.get('RequiredSkillID', 0) if qta else 0}
    GetRequiredSkillValue()    = {qta.get('RequiredSkillPoints', 0) if qta else 0}
    GetRepObjectiveFaction()   = {qt.get('RequiredFactionId1', 0)}
    GetRepObjectiveValue()     = {qt.get('RequiredFactionValue1', 0)}
    GetRepObjectiveFaction2()  = {qt.get('RequiredFactionId2', 0)}
    GetRepObjectiveValue2()    = {qt.get('RequiredFactionValue2', 0)}
    GetRequiredMinRepFaction() = {qta.get('RequiredMinRepFaction', 0) if qta else 0}
    GetRequiredMinRepValue()   = {qta.get('RequiredMinRepValue', 0) if qta else 0}
    GetRequiredMaxRepFaction() = {qta.get('RequiredMaxRepFaction', 0) if qta else 0}
    GetRequiredMaxRepValue()   = {qta.get('RequiredMaxRepValue', 0) if qta else 0}
    GetRewMailTemplateId()     = {qta.get('RewardMailTemplateID', 0) if qta else 0}
    GetRewMailDelaySecs()      = {qta.get('RewardMailDelay', 0) if qta else 0}
    GetRewMailSenderEntry()    = (from quest_mail_sender table)
    GetSrcSpell()              = {qta.get('SourceSpellID', 0) if qta else 0}

  Character Quest Status (from character_queststatus table):
    status      = 0=None, 1=COMPLETE, 3=INCOMPLETE, 5=FAILED, 6=REWARDED
    explored    = 0/1 (exploration objective met)
    timer       = remaining time in ms
    mobcount1-4 = current kill/credit count per objective
    itemcount1-6= current item count per item objective
    playercount = current player kill count
""")

    # ── JSON dump ────────────────────────────────────────────────────────────
    print(f"\n── Full JSON Data ──")
    # Convert any non-serializable types
    def default_serializer(obj):
        if isinstance(obj, (bytes, bytearray)):
            return obj.decode('utf-8', errors='replace')
        if isinstance(obj, (set, frozenset)):
            return list(obj)
        return str(obj)

    print(json.dumps(data, indent=2, default=default_serializer, ensure_ascii=False))


# ── Main ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) > 1:
        quest_ids = [int(x) for x in sys.argv[1:]]
    else:
        # Default: two classic quests with diverse objectives
        # 826 = Zalazane (kill + item objective)
        # 131 = The Legend of Stalvan (investigation/story quest)
        quest_ids = [826, 131]

    for qid in quest_ids:
        data = extract_quest(qid)
        if data:
            print_quest_report(data)
