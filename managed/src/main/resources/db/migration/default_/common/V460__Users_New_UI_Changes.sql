-- Copyright (c) YugabyteDB, Inc.
-- Disable new universe UI for existing users and for new users by default.
ALTER TABLE IF EXISTS users ALTER COLUMN new_universe_ui_enabled SET DEFAULT FALSE;
UPDATE users SET new_universe_ui_enabled = FALSE;
-- Persistent per-user key-value settings (server-side localStorage analog).
ALTER TABLE IF EXISTS users ADD COLUMN IF NOT EXISTS settings json;
-- Rename config name.
UPDATE runtime_config_entry SET path = 'yb.ui.feature_flags.enable_new_universe_experience' WHERE path = 'yb.ui.feature_flags.edit_universe_v2_ui_enabled';