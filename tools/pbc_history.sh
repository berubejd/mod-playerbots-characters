#!/usr/bin/env bash
# pbc_history.sh — Show the last N history messages for a PBC character.
# Usage: ./pbc_history.sh <CharacterName> [N]
#        CharacterName — character to show history for (required)
#        N             — number of messages (default: 5)

if [[ $# -lt 1 || -z "$1" ]]; then
    echo "Usage: $0 <CharacterName> [N]"
    exit 1
fi

CHAR_NAME="$1"
N=${2:-5}
SAFE_NAME="${CHAR_NAME//\'/\'\'}"

ROWS=$(mysql -NB acore_characters 2>/dev/null <<SQL
SELECT c.name, h.timestamp, h.message
FROM (
    SELECT bot_guid, id, timestamp, message
    FROM mod_pbc_chat_history
    WHERE bot_guid = (SELECT guid FROM characters WHERE name = '${SAFE_NAME}' LIMIT 1)
    ORDER BY id DESC
    LIMIT ${N}
) h
JOIN characters c ON c.guid = h.bot_guid
ORDER BY h.id ASC;
SQL
)

if [[ -z "$ROWS" ]]; then
    echo "No history found."
    exit 0
fi

current_name=""
while IFS=$'\t' read -r name timestamp message; do
    if [[ "$name" != "$current_name" ]]; then
        if [[ -n "$current_name" ]]; then
            echo ""
        fi
        echo "════════════════════════════════════════"
        echo "  ${name}"
        echo "════════════════════════════════════════"
        current_name="$name"
    fi
    echo ""
    echo "  [${timestamp}]"
    echo "${message}" | fold -s -w 76 | sed 's/^/    /'
done <<< "$ROWS"

echo ""
