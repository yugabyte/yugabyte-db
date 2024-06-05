-- Copyright (c) YugaByte, Inc.

ALTER TABLE alert_definition ADD COLUMN IF NOT EXISTS active boolean DEFAULT TRUE NOT NULL;

UPDATE alert_definition SET active = false WHERE uuid IN
  (SELECT definition_uuid FROM alert_definition_label WHERE name = 'source_uuid' AND value in
     (SELECT universe_uuid::text FROM universe WHERE universe_details_json LIKE '%"universePaused":true%'));

UPDATE alert_definition SET config_written = false;
