#!/usr/bin/env bash
# pbc_copy_history.sh — Copy non-whisper chat history from one character to another.
#
# Useful when enabling PBC.TrackPlayerCharacter for an existing character
# whose events were previously only tracked under their bot counterpart.
#
# Usage: ./pbc_copy_history.sh <FromName> <ToName>
#   FromName — copy history FROM this character (required)
#   ToName   — copy history TO this character   (required)

set -euo pipefail

if [[ $# -lt 2 || -z "$1" || -z "$2" ]]; then
    echo "Usage: $0 <FromName> <ToName>"
    exit 1
fi

FROM_NAME="$1"
TO_NAME="$2"
SAFE_FROM="${FROM_NAME//\'/\'\'}"
SAFE_TO="${TO_NAME//\'/\'\'}"

# ------------------------------------------------------------------
# Validate both characters exist
# ------------------------------------------------------------------
FROM_GUID=$(mysql -NB acore_characters 2>/dev/null \
    -e "SELECT guid FROM characters WHERE name = '${SAFE_FROM}' LIMIT 1;")
TO_GUID=$(mysql -NB acore_characters 2>/dev/null \
    -e "SELECT guid FROM characters WHERE name = '${SAFE_TO}' LIMIT 1;")

if [[ -z "$FROM_GUID" ]]; then
    echo "ERROR: Character '${FROM_NAME}' not found."
    exit 1
fi
if [[ -z "$TO_GUID" ]]; then
    echo "ERROR: Character '${TO_NAME}' not found."
    exit 1
fi

# ------------------------------------------------------------------
# Dry-run: count total and non-whisper rows for source
# ------------------------------------------------------------------
TOTAL_ROWS=$(mysql -NB acore_characters 2>/dev/null \
    -e "SELECT COUNT(*) FROM mod_pbc_chat_history WHERE bot_guid = ${FROM_GUID};")
COPY_ROWS=$(mysql -NB acore_characters 2>/dev/null \
    -e "SELECT COUNT(*) FROM mod_pbc_chat_history WHERE bot_guid = ${FROM_GUID} AND message NOT LIKE '%(privately to%';")
WHISPER_ROWS=$(( TOTAL_ROWS - COPY_ROWS ))

echo "═══════════════════════════════════════════════════════════════"
echo "  PBC Copy History"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "  From:  ${FROM_NAME}  (GUID: ${FROM_GUID})"
echo "  To:    ${TO_NAME}    (GUID: ${TO_GUID})"
echo ""
echo "  Source history:"
echo "    Total messages:  ${TOTAL_ROWS}"
echo "    Non-whisper:     ${COPY_ROWS}   (will be copied)"
echo "    Whispers:        ${WHISPER_ROWS}   (will be skipped)"
echo ""

# ------------------------------------------------------------------
# Show a few sample messages that will be copied
# ------------------------------------------------------------------
if [[ "$COPY_ROWS" -gt 0 ]]; then
    echo "  Sample messages to copy (up to 3):"
    SAMPLES=$(mysql -NB acore_characters 2>/dev/null <<SQL
SELECT CONCAT('    [', h.timestamp, '] ', LEFT(h.message, 120))
FROM mod_pbc_chat_history h
WHERE h.bot_guid = ${FROM_GUID}
  AND h.message NOT LIKE '%(privately to%'
ORDER BY h.id ASC
LIMIT 3;
SQL
)
    echo "$SAMPLES"
    if [[ "$COPY_ROWS" -gt 3 ]]; then
        echo "    ... and $(( COPY_ROWS - 3 )) more"
    fi
    echo ""
fi

# ------------------------------------------------------------------
# Confirm
# ------------------------------------------------------------------
read -r -p "  Proceed with copy? [y/N] " REPLY
echo ""
if [[ ! "$REPLY" =~ ^[Yy]$ ]]; then
    echo "  Cancelled."
    exit 0
fi

# ------------------------------------------------------------------
# Execute
# ------------------------------------------------------------------
mysql acore_characters 2>/dev/null <<SQL
INSERT INTO mod_pbc_chat_history (bot_guid, \`timestamp\`, message)
SELECT
    ${TO_GUID}          AS bot_guid,
    h.\`timestamp\`,
    h.message
FROM mod_pbc_chat_history h
WHERE h.bot_guid = ${FROM_GUID}
  AND h.message NOT LIKE '%(privately to%';
SQL

echo "  Done — copied ${COPY_ROWS} message(s) from '${FROM_NAME}' to '${TO_NAME}'."
echo "═══════════════════════════════════════════════════════════════"
