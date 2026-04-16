ALTER TABLE IF EXISTS provider ADD COLUMN IF NOT EXISTS details json_alias;
ALTER TABLE provider ALTER COLUMN details TYPE binary varying;
ALTER TABLE region ALTER COLUMN details TYPE binary varying;
ALTER TABLE IF EXISTS availability_zone ADD COLUMN IF NOT EXISTS details json_alias;
ALTER TABLE availability_zone ALTER COLUMN details TYPE binary varying;
