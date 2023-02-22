-- Copyright (c) YugaByte, Inc.

ALTER TABLE alert_configuration ADD COLUMN IF NOT EXISTS default_destination boolean DEFAULT FALSE NOT NULL;
UPDATE alert_configuration SET default_destination = TRUE WHERE destination_uuid is null;
