-- mod-playerbots-characters: initial schema
-- Creates all required tables if they do not already exist.

-- Stores condensed history appended to a character's card (in the DB, not on disk).
CREATE TABLE IF NOT EXISTS `mod_pbc_character_card_additions` (
    `id`            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `bot_guid`      BIGINT UNSIGNED NOT NULL COMMENT 'GUID of the playerbot character',
    `addition`      MEDIUMTEXT NOT NULL         COMMENT 'Condensed text appended to the base character card',
    `created_at`    DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX `idx_bot_guid_created` (`bot_guid`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='LLM-condensed additions to bot character cards';

-- Per-bot chat and event history lines for LLM context.
-- Each row is one pre-formatted single-line string, e.g.:
--   "John: Hello, how are you?"
--   "*John picked up [Thunderfury, Blessed Blade of the Windseeker]*"
CREATE TABLE IF NOT EXISTS `mod_pbc_chat_history` (
    `id`        BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `bot_guid`  BIGINT UNSIGNED NOT NULL COMMENT 'GUID of the playerbot this history belongs to',
    `timestamp` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `message`   MEDIUMTEXT NOT NULL COMMENT 'Pre-formatted chat or event line (single-line, no embedded newlines)',
    INDEX `idx_bot_guid` (`bot_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Per-bot chat and event history lines for LLM context';

-- Per-bot persistent data: location tracking and roll chance modifier.
-- Written when a bot's location event fires or when roll_modifier is set;
-- loaded on server start so that the stable-cycle counter is not reset by
-- a server restart and roll modifiers are preserved.
CREATE TABLE IF NOT EXISTS `mod_pbc_data` (
    `bot_guid`              BIGINT UNSIGNED NOT NULL PRIMARY KEY COMMENT 'GUID of the playerbot character',
    `last_location`         VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'Last "Area in Zone" string for which the location event was fired',
    `roll_chance_modifier`  INT NOT NULL DEFAULT 0 COMMENT 'Per-character roll chance modifier (-100 to 100), added to every roll chance',
    `updated_at`            DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Per-bot persistent data (location, roll modifier)';

-- LLM-generated relationship descriptions per bot, one row per (bot, target) pair.
CREATE TABLE IF NOT EXISTS `mod_pbc_relationships` (
    `id`                         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `bot_guid`                   BIGINT UNSIGNED NOT NULL COMMENT 'GUID of the playerbot that owns this relationship',
    `target_name`                VARCHAR(64)     NOT NULL COMMENT 'Name of the other character',
    `relationship_text`          MEDIUMTEXT      NOT NULL COMMENT 'LLM-generated relationship description',
    `mention_count_at_last_update` INT UNSIGNED  NOT NULL DEFAULT 0 COMMENT 'Total mention count in history at the time of the last relationship update',
    `updated_at`                 DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY `uq_bot_target` (`bot_guid`, `target_name`),
    INDEX `idx_bot_guid` (`bot_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='LLM-generated per-bot relationship descriptions with other characters';
