#!/usr/bin/env bash
# pbc_history.sh — Show the last N history messages for PBC characters.
# Usage: ./pbc_history.sh [N [CharacterName]]
#        N             — number of messages per character (default: 5)
#        CharacterName — optional; restrict output to this character only
#        ./pbc_history.sh backup — dump full history to an SQL file in current dir

if [[ "$1" == "backup" ]]; then
    TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")
    OUTFILE="pbc_chat_history_${TIMESTAMP}.sql"
    mysqldump --no-tablespaces acore_characters mod_pbc_chat_history 2>/dev/null > "$OUTFILE"
    if [[ $? -eq 0 && -s "$OUTFILE" ]]; then
        echo "Backup saved to: $OUTFILE"
    else
        echo "Backup failed or table is empty."
        rm -f "$OUTFILE"
        exit 1
    fi
    exit 0
fi

N=${1:-5}
CHAR_NAME="${2:-}"
SAFE_NAME="${CHAR_NAME//\'/\'\'}"

if [[ -n "$CHAR_NAME" ]]; then
    ROWS=$(mysql -NB acore_characters 2>/dev/null <<SQL
SELECT c.name, h.timestamp, h.message
FROM mod_pbc_chat_history h
JOIN characters c ON c.guid = h.bot_guid
WHERE c.name = '${SAFE_NAME}'
  AND (h.bot_guid, h.id) IN (
      SELECT bot_guid, id FROM (
          SELECT bot_guid, id,
                 ROW_NUMBER() OVER (PARTITION BY bot_guid ORDER BY id DESC) AS rn
          FROM mod_pbc_chat_history
          WHERE bot_guid = (SELECT guid FROM characters WHERE name = '${SAFE_NAME}' LIMIT 1)
      ) ranked
      WHERE rn <= ${N}
  )
ORDER BY h.id ASC;
SQL
    )
else
    ROWS=$(mysql -NB acore_characters 2>/dev/null <<SQL
SELECT c.name, h.timestamp, h.message
FROM mod_pbc_chat_history h
JOIN characters c ON c.guid = h.bot_guid
WHERE (h.bot_guid, h.id) IN (
    SELECT bot_guid, id FROM (
        SELECT bot_guid, id,
               ROW_NUMBER() OVER (PARTITION BY bot_guid ORDER BY id DESC) AS rn
        FROM mod_pbc_chat_history
    ) ranked
    WHERE rn <= ${N}
)
ORDER BY c.name ASC, h.id ASC;
SQL
    )
fi

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
