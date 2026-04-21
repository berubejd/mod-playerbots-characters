-- mod-playerbots-characters: migration 20260421
-- Renames mod_pbc_bot_location to mod_pbc_data and adds roll_chance_modifier column.
-- Preserves all existing data.
-- Safe to run on both fresh installs (table already exists) and upgrades (table needs renaming).

-- Rename the old table if it exists (upgrade path).
-- On fresh installs where mod_pbc_data already exists, this is a no-op.
SET @old_table_exists = (SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_bot_location');
SET @new_table_exists = (SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_data');

-- Rename old table to new name (only if old exists and new doesn't)
SET @sql = IF(@old_table_exists > 0 AND @new_table_exists = 0,
    'RENAME TABLE `mod_pbc_bot_location` TO `mod_pbc_data`',
    'SELECT ''Skip rename: mod_pbc_data already exists or mod_pbc_bot_location does not exist'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Add roll_chance_modifier column if it doesn't exist yet
SET @col_exists = (SELECT COUNT(*) FROM information_schema.columns
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_data' AND column_name = 'roll_chance_modifier');

SET @sql = IF(@col_exists = 0,
    'ALTER TABLE `mod_pbc_data` ADD COLUMN `roll_chance_modifier` INT NOT NULL DEFAULT 0 COMMENT ''Per-character roll chance modifier (-100 to 100), added to every roll chance'' AFTER `last_location`',
    'SELECT ''Skip add column: roll_chance_modifier already exists'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Update the table comment
ALTER TABLE `mod_pbc_data` COMMENT='Per-bot persistent data (location, roll modifier)';
