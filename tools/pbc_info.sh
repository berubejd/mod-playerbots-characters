#!/usr/bin/env bash
# pbc_info.sh — Show card additions and relationships for a PBC character.
# Usage: ./pbc_info.sh <CharacterName>

if [[ -z "$1" ]]; then
    echo "Usage: $0 <CharacterName>"
    exit 1
fi

CHAR_NAME="$1"
SAFE_NAME="${CHAR_NAME//\'/\'\'}"

# ── resolve GUID ──────────────────────────────────────────────────────────────
GUID=$(mysql -NB acore_characters 2>/dev/null \
    -e "SELECT guid FROM characters WHERE name = '${SAFE_NAME}' LIMIT 1;")

if [[ -z "$GUID" ]]; then
    echo "Character '${CHAR_NAME}' not found."
    exit 1
fi

# ── card additions ────────────────────────────────────────────────────────────
ADDITIONS=$(mysql -NB acore_characters 2>/dev/null <<SQL
SELECT id, created_at, addition
FROM mod_pbc_character_card_additions
WHERE bot_guid = ${GUID}
ORDER BY created_at ASC;
SQL
)

echo "════════════════════════════════════════"
echo "  Character: ${CHAR_NAME}  (guid: ${GUID})"
echo "════════════════════════════════════════"

echo ""
echo "── Card Additions ───────────────────────"
if [[ -z "$ADDITIONS" ]]; then
    echo "(none)"
else
    idx=1
    while IFS=$'\t' read -r id created_at addition; do
        echo ""
        echo "  [#${idx}] ${created_at}"
        echo "${addition}" | fold -s -w 76 | sed 's/^/    /'
        (( idx++ ))
    done <<< "$ADDITIONS"
fi

# ── roll modifier ─────────────────────────────────────────────────────────────
ROLL_MOD=$(mysql -NB acore_characters 2>/dev/null <<SQL
SELECT roll_chance_modifier
FROM mod_pbc_data
WHERE bot_guid = ${GUID};
SQL
)

echo ""
echo "── Roll Modifier ────────────────────────"
if [[ -z "$ROLL_MOD" ]]; then
    echo "  0 (default)"
else
    echo "  ${ROLL_MOD}"
fi

# ── relationships ─────────────────────────────────────────────────────────────
RELS=$(mysql -NB acore_characters 2>/dev/null <<SQL
SELECT target_name, updated_at, mention_count_at_last_update, relationship_text
FROM mod_pbc_relationships
WHERE bot_guid = ${GUID}
ORDER BY target_name ASC;
SQL
)

echo ""
echo "── Relationships ────────────────────────"
if [[ -z "$RELS" ]]; then
    echo "(none)"
else
    while IFS=$'\t' read -r target updated_at mentions rel_text; do
        echo ""
        echo "  → ${target}  (updated: ${updated_at}, mentions at update: ${mentions})"
        echo "${rel_text}" | fold -s -w 76 | sed 's/^/    /'
    done <<< "$RELS"
fi

echo ""
