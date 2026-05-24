-- ============================================================================
-- Migration: Chat History Normalization
--
-- Ensure consistent SQL mode — disable ONLY_FULL_GROUP_BY for the
-- GROUP BY dedup step (restored at end of migration).
-- ============================================================================
SET @prev_sql_mode = @@sql_mode;
SET @@sql_mode = REPLACE(REPLACE(REPLACE(@@sql_mode, 'ONLY_FULL_GROUP_BY,', ''), 'ONLY_FULL_GROUP_BY', ''), ',,', ',');

-- ============================================================================
-- Migration: Chat History Normalization
-- Replaces mod_pbc_chat_history with two normalized tables:
--   mod_pbc_history        — central message store (one row per unique message)
--   mod_pbc_history_owners — junction table (which characters see each message)
--
-- Message parsing:
--   "Narrator: *text*"                       → type=0, author=0,  message="text"
--   "Name (privately to you): text"          → type=7, author=Name, message="text"
--   "Name (privately to Other): text"        → type=7, author=Name, message="text"
--   "Name: text"                             → type=2, author=Name, message="text"
--
-- Dedup: (author_guid, type, message_hash) unique → one mod_pbc_history row
-- Whisper merging: same author + same text → single row with all recipients
--
-- Uses a temporary CRC32 hash column for dedup (removed after migration).
-- All conditional logic uses prepared statements (no DELIMITER/stored procedures).
-- ============================================================================

-- ============================================================================
-- Part A: Create New Tables
-- ============================================================================

CREATE TABLE IF NOT EXISTS `mod_pbc_history` (
    `id`          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `timestamp`   DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `author_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'GUID of the character who spoke, 0 for narrator messages',
    `type`        TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT 'Chat type: 0=narrator, 1=SAY, 2=PARTY, 6=YELL, 7=WHISPER (matches ChatMsg enum in SharedDefines.h)',
    `message`     MEDIUMTEXT NOT NULL COMMENT 'Raw message text without speaker prefix (e.g. "Hello!" not "John: Hello!")',
    INDEX `idx_author_guid` (`author_guid`),
    INDEX `idx_timestamp` (`timestamp`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Central chat history — one row per unique message, shared across characters via mod_pbc_history_owners';

CREATE TABLE IF NOT EXISTS `mod_pbc_history_owners` (
    `guid`       BIGINT UNSIGNED NOT NULL COMMENT 'Character GUID that owns/views this message in their history',
    `history_id` BIGINT UNSIGNED NOT NULL COMMENT 'FK to mod_pbc_history.id',
    PRIMARY KEY (`guid`, `history_id`),
    INDEX `idx_history_id` (`history_id`),
    CONSTRAINT `fk_history_owner_message`
        FOREIGN KEY (`history_id`) REFERENCES `mod_pbc_history`(`id`)
        ON DELETE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Maps characters to their chat history messages (junction table)';

-- ============================================================================
-- Part B: Migrate Data (conditional — only if old table exists and not yet migrated)
-- ============================================================================

-- Determine whether migration is needed
SET @old_exists = (SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_chat_history');

SET @new_has_data = (SELECT COUNT(*) FROM mod_pbc_history);

SET @do_migrate = IF(@old_exists > 0 AND @new_has_data = 0, 1, 0);

-- ------------------------------------------------------------------------
-- Step B1: Add temporary hash column and unique index for dedup
-- ------------------------------------------------------------------------
SET @sql = IF(@do_migrate = 1,
    'ALTER TABLE `mod_pbc_history` ADD COLUMN `message_hash` BIGINT UNSIGNED NOT NULL DEFAULT 0, ADD UNIQUE INDEX `idx_dedup` (`author_guid`, `type`, `message_hash`)',
    'SELECT ''Skip B1: migration not needed'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ------------------------------------------------------------------------
-- Step B2: Create temporary table for parsed data
-- ------------------------------------------------------------------------
SET @sql = IF(@do_migrate = 1,
    'CREATE TEMPORARY TABLE `tmp_parsed` (
        `old_id`      BIGINT UNSIGNED NOT NULL PRIMARY KEY,
        `bot_guid`    BIGINT UNSIGNED NOT NULL,
        `timestamp`   DATETIME NOT NULL,
        `author_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
        `type`        TINYINT UNSIGNED NOT NULL DEFAULT 2,
        `message`     MEDIUMTEXT NOT NULL,
        `message_hash` BIGINT UNSIGNED NOT NULL DEFAULT 0,
        INDEX `idx_lookup` (`author_guid`, `type`, `message_hash`)
    ) ENGINE=InnoDB',
    'SELECT ''Skip B2: migration not needed'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ------------------------------------------------------------------------
-- Step B3: Parse narrator messages — "Narrator: *text*"
-- ------------------------------------------------------------------------
SET @sql = IF(@do_migrate = 1,
    'INSERT INTO `tmp_parsed` (`old_id`, `bot_guid`, `timestamp`, `author_guid`, `type`, `message`, `message_hash`)
     SELECT `id`, `bot_guid`, `timestamp`, 0, 0,
            TRIM(TRAILING ''*'' FROM SUBSTRING(`message`, 12)),
            CRC32(TRIM(TRAILING ''*'' FROM SUBSTRING(`message`, 12)))
     FROM `mod_pbc_chat_history`
     WHERE `message` LIKE ''Narrator: *%''',
    'SELECT ''Skip B3: migration not needed'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ------------------------------------------------------------------------
-- Step B4: Parse whisper messages — "Name (privately to X): text"
-- ------------------------------------------------------------------------
SET @sql = IF(@do_migrate = 1,
    'INSERT INTO `tmp_parsed` (`old_id`, `bot_guid`, `timestamp`, `author_guid`, `type`, `message`, `message_hash`)
     SELECT h.`id`, h.`bot_guid`, h.`timestamp`, COALESCE(c.`guid`, 0), 7,
            SUBSTRING_INDEX(h.`message`, ''): '', -1),
            CRC32(SUBSTRING_INDEX(h.`message`, ''): '', -1))
     FROM `mod_pbc_chat_history` h
     LEFT JOIN `characters` c ON c.`name` = SUBSTRING_INDEX(h.`message`, '' (privately to '', 1)
     WHERE h.`message` LIKE ''% (privately to %): %''',
    'SELECT ''Skip B4: migration not needed'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ------------------------------------------------------------------------
-- Step B5: Parse regular chat messages — "Name: text"
-- ------------------------------------------------------------------------
SET @sql = IF(@do_migrate = 1,
    'INSERT INTO `tmp_parsed` (`old_id`, `bot_guid`, `timestamp`, `author_guid`, `type`, `message`, `message_hash`)
     SELECT h.`id`, h.`bot_guid`, h.`timestamp`, COALESCE(c.`guid`, 0), 2,
            SUBSTRING(h.`message`, LOCATE('': '', h.`message`) + 2),
            CRC32(SUBSTRING(h.`message`, LOCATE('': '', h.`message`) + 2))
     FROM `mod_pbc_chat_history` h
     LEFT JOIN `characters` c ON c.`name` = SUBSTRING_INDEX(h.`message`, '': '', 1)
     WHERE h.`message` NOT LIKE ''Narrator: *%''
       AND h.`message` NOT LIKE ''% (privately to %): %''
       AND h.`message` LIKE ''%: %''',
    'SELECT ''Skip B5: migration not needed'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ------------------------------------------------------------------------
-- Step B6: Dedup — insert unique messages into mod_pbc_history
-- ------------------------------------------------------------------------
SET @sql = IF(@do_migrate = 1,
    'INSERT IGNORE INTO `mod_pbc_history` (`timestamp`, `author_guid`, `type`, `message`, `message_hash`)
     SELECT MIN(`timestamp`), `author_guid`, `type`, `message`, `message_hash`
     FROM `tmp_parsed`
     GROUP BY `author_guid`, `type`, `message_hash`
     ORDER BY MIN(`old_id`) ASC',
    'SELECT ''Skip B6: migration not needed'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ------------------------------------------------------------------------
-- Step B7: Map ownership — link old bot_guid to new history_id
-- ------------------------------------------------------------------------
SET @sql = IF(@do_migrate = 1,
    'INSERT IGNORE INTO `mod_pbc_history_owners` (`guid`, `history_id`)
     SELECT t.`bot_guid`, h.`id`
     FROM `tmp_parsed` t
     JOIN `mod_pbc_history` h ON h.`author_guid` = t.`author_guid`
                             AND h.`type` = t.`type`
                             AND h.`message_hash` = t.`message_hash`',
    'SELECT ''Skip B7: migration not needed'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ------------------------------------------------------------------------
-- Step B8: Clean up temporary structures
-- ------------------------------------------------------------------------
SET @sql = IF(@do_migrate = 1,
    'DROP TEMPORARY TABLE IF EXISTS `tmp_parsed`',
    'SELECT ''Skip B8a: migration not needed'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql = IF(@do_migrate = 1,
    'ALTER TABLE `mod_pbc_history` DROP INDEX `idx_dedup`, DROP COLUMN `message_hash`',
    'SELECT ''Skip B8b: migration not needed'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ============================================================================
-- Part C: Verify & Clean Up
-- ============================================================================

-- Pre-compute old row count before dropping (must use prepared statement
-- because MySQL IF() evaluates all branches even the unchosen one).
SET @sql = IF(@old_exists > 0,
    'SET @old_row_count = (SELECT COUNT(*) FROM `mod_pbc_chat_history`)',
    'SET @old_row_count = 0');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SELECT '--- Migration Sanity Checks ---' AS info;

SELECT 'Old row count' AS metric, @old_row_count AS value
UNION ALL
SELECT 'New message count (should be <= old)', COUNT(*) FROM `mod_pbc_history`
UNION ALL
SELECT 'Owner count (<= old row count; duplicates collapsed)', COUNT(*) FROM `mod_pbc_history_owners`;

-- Drop old table
DROP TABLE IF EXISTS `mod_pbc_chat_history`;

-- Restore previous SQL mode
SET @@sql_mode = @prev_sql_mode;

SELECT 'Migration complete: mod_pbc_chat_history has been replaced by mod_pbc_history + mod_pbc_history_owners.' AS status;
