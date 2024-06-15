ALTER TABLE release_local_file
ADD COLUMN IF NOT EXISTS is_upload BOOLEAN DEFAULT false;