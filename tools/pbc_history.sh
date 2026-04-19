#!/usr/bin/env bash
# pbc_history.sh — Show the last N history messages for each PBC character,
#                  or back up the full chat history to an SQL file.
# Usage: ./pbc_history.sh [N]        (default: 5) — show last N messages
#        ./pbc_history.sh backup     — dump full history to SQL file in current dir

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

# Map of guid -> character name (update if guids change)
declare -A NAMES=(
    [655]="Luna"
    [656]="Diana"
    [657]="Jon"
    [708]="Yavo"
)

GUIDS=(655 656 657 708)
GUID_LIST=$(IFS=,; echo "${GUIDS[*]}")

# Fetch last N rows per bot, ordered ascending within each bot
ROWS=$(mysql -NB acore_characters 2>/dev/null <<SQL
SELECT bot_guid, message
FROM mod_pbc_chat_history
WHERE (bot_guid, id) IN (
    SELECT bot_guid, id
    FROM (
        SELECT bot_guid, id,
               ROW_NUMBER() OVER (PARTITION BY bot_guid ORDER BY id DESC) AS rn
        FROM mod_pbc_chat_history
        WHERE bot_guid IN (${GUID_LIST})
    ) ranked
    WHERE rn <= ${N}
)
ORDER BY bot_guid ASC, id ASC;
SQL
)

if [[ -z "$ROWS" ]]; then
    echo "No history found."
    exit 0
fi

current_guid=""
while IFS=$'\t' read -r guid message; do
    if [[ "$guid" != "$current_guid" ]]; then
        [[ -n "$current_guid" ]] && echo ""
        name="${NAMES[$guid]:-guid:$guid}"
        echo "[${name}]"
        current_guid="$guid"
    fi
    echo "$message"
done <<< "$ROWS"
