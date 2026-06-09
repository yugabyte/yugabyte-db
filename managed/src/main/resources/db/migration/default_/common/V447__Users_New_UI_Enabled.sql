-- Copyright (c) YugabyteDB, Inc.
ALTER TABLE IF EXISTS users ADD COLUMN IF NOT EXISTS new_universe_ui_enabled boolean default TRUE;
