-- ============================================================================
-- Migration: DB-canonical character cards + memory attribution
--
-- 1. mod_pbc_cards — structured, DB-canonical character identity (all persona
--    fields optional).  Keyed on the owning entity's GUID so a future NPC
--    module can reuse this exact shape with no schema change.  Rendering is a
--    pure function of these stored fields plus live attributes — there is
--    NEVER a model call at render/load time.
-- 2. mod_pbc_history — short-term memory attribution captured at ingestion.
-- 3. mod_pbc_memories — long-term memory enrichment (attribution propagated
--    from history at condensation) + lifecycle bookkeeping.
--
-- Additive and idempotent (guarded), so a rehash re-run is safe.
-- All conditional logic uses prepared statements (no DELIMITER / procedures).
-- ============================================================================

-- ----------------------------------------------------------------------------
-- 1. mod_pbc_cards
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `mod_pbc_cards` (
    `bot_guid`       BIGINT UNSIGNED NOT NULL PRIMARY KEY COMMENT 'Owning entity GUID (playerbot today, NPC-forward-compatible)',
    `name`           VARCHAR(64)  NOT NULL DEFAULT ''      COMMENT 'Denormalized name; lookup + disk-import match key only',
    `premise`        TEXT         DEFAULT NULL,
    `personality`    TEXT         DEFAULT NULL,
    `values`         TEXT         DEFAULT NULL,
    `background`     TEXT         DEFAULT NULL,
    `speech_style`   TEXT         DEFAULT NULL,
    `quirks`         TEXT         DEFAULT NULL,
    `provenance`     ENUM('generated','edited','override') NOT NULL DEFAULT 'generated' COMMENT 'Resolution precedence: override/pinned > edited > generated',
    `pinned`         TINYINT(1)   NOT NULL DEFAULT 0       COMMENT 'Disk import sets 1; while pinned the file is authoritative and edits are rejected',
    `card_file_hash` CHAR(64)     DEFAULT NULL             COMMENT 'SHA-256 of the imported disk file; re-import signal on change',
    `gen_model`      VARCHAR(64)  DEFAULT NULL             COMMENT 'Model used at generation time',
    `gen_version`    INT UNSIGNED NOT NULL DEFAULT 1,
    `created_at`     TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at`     TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX `idx_name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='DB-canonical structured character identity cards (all persona fields optional)';

-- ----------------------------------------------------------------------------
-- 2. mod_pbc_history attribution enrichment (short-term memory)
--    subject_guid — who the event is about (NULL when not applicable)
--    event_type   — server-derived category (item/duel/levelup/quest/...),
--                   distinct from the existing `type` (chat channel)
--    mood         — model-free type->mood at ingestion, optionally AI-refined
--    Guard: presence of `subject_guid` gates the whole ALTER.
-- ----------------------------------------------------------------------------
SET @hist_col = (SELECT COUNT(*) FROM information_schema.columns
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_history' AND column_name = 'subject_guid');
SET @sql = IF(@hist_col = 0,
    'ALTER TABLE `mod_pbc_history`
        ADD COLUMN `subject_guid` BIGINT UNSIGNED DEFAULT NULL COMMENT ''Who this event is about; stamped at ingestion'' AFTER `author_guid`,
        ADD COLUMN `event_type`   VARCHAR(32) DEFAULT NULL COMMENT ''Server-derived event category (distinct from chat type)'',
        ADD COLUMN `mood`         VARCHAR(32) DEFAULT NULL COMMENT ''Character mood for this event; lookup at ingestion, optional AI refine'',
        ADD INDEX `idx_subject_guid` (`subject_guid`)',
    'SELECT ''Skip mod_pbc_history enrichment: subject_guid already exists'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ----------------------------------------------------------------------------
-- 3. mod_pbc_memories enrichment (long-term memory): attribution + type +
--    mood + lifecycle.  Populated by propagation from short-term history at
--    condensation time.  VARCHAR(32) for `type`/`mood` deliberately (avoids
--    ENUM migration churn).
--    Guard: presence of `subject_guid` gates the whole ALTER.
-- ----------------------------------------------------------------------------
SET @mem_col = (SELECT COUNT(*) FROM information_schema.columns
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_memories' AND column_name = 'subject_guid');
SET @sql = IF(@mem_col = 0,
    'ALTER TABLE `mod_pbc_memories`
        ADD COLUMN `subject_guid` BIGINT UNSIGNED DEFAULT NULL COMMENT ''Who this memory is about; propagated from the source event'' AFTER `bot_guid`,
        ADD COLUMN `type`         VARCHAR(32) NOT NULL DEFAULT ''general'' COMMENT ''Event-derived category; VARCHAR to avoid ENUM migration churn'',
        ADD COLUMN `mood`         VARCHAR(32) DEFAULT NULL COMMENT ''Mood carried from the source event window'',
        ADD COLUMN `active`       TINYINT(1) NOT NULL DEFAULT 1 COMMENT ''0 = retired from selection'',
        ADD COLUMN `used`         TINYINT(1) NOT NULL DEFAULT 0 COMMENT ''1 = surfaced in a prompt at least once (deprioritized)'',
        ADD COLUMN `last_used_at` TIMESTAMP NULL DEFAULT NULL COMMENT ''When this memory was last surfaced'',
        ADD INDEX `idx_bot_subject` (`bot_guid`,`subject_guid`),
        ADD INDEX `idx_bot_active`  (`bot_guid`,`active`)',
    'SELECT ''Skip mod_pbc_memories enrichment: subject_guid already exists'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SELECT 'Migration complete: mod_pbc_cards created; history + memories attribution columns added.' AS status;
