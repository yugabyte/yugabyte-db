ALTER TABLE alert ADD COLUMN IF NOT EXISTS source_uuid uuid;
UPDATE alert SET source_uuid = (SELECT universe_uuid FROM universe WHERE name = alert.source_name);