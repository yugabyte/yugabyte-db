ALTER TABLE universe_details ADD COLUMN last_sync_status boolean;
ALTER TABLE universe_details ADD COLUMN last_sync_timestamp timestamptz;
ALTER TABLE universe_details ADD COLUMN last_successful_sync_timestamp timestamptz;
ALTER TABLE universe_details ADD COLUMN last_sync_error text;
