#!/usr/bin/env bash
# pbc_backup.sh — Backup all PBC tables to a single SQL file.
# Usage: ./pbc_backup.sh [output_path]
#        output_path — optional; defaults to pbc_backup_<timestamp>.sql in current dir

TABLES=(
    mod_pbc_history
    mod_pbc_history_owners
    mod_pbc_memories
    mod_pbc_data
    mod_pbc_relationships
)

TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")
OUTFILE="${1:-pbc_backup_${TIMESTAMP}.sql}"

mysqldump --no-tablespaces acore_characters "${TABLES[@]}" 2>/dev/null > "$OUTFILE"
if [[ $? -eq 0 && -s "$OUTFILE" ]]; then
    echo "Backup saved to: $OUTFILE"
    echo "Tables included: ${TABLES[*]}"
else
    echo "Backup failed or all tables are empty."
    rm -f "$OUTFILE"
    exit 1
fi
