-- Remove mention_count_at_last_update column — relationship updates are now
-- triggered only on condensation (manual or automatic), not by mention threshold.
ALTER TABLE `mod_pbc_relationships` DROP COLUMN `mention_count_at_last_update`;
