-- Copyright (c) YugaByte, Inc.

ALTER TABLE schedule ADD COLUMN IF NOT EXISTS schedule_name varchar(255);
ALTER TABLE schedule ADD COLUMN IF NOT EXISTS owner_uuid uuid;

UPDATE schedule SET schedule_name = (('schedule-') ||  schedule_uuid);
UPDATE schedule SET owner_uuid = ((task_params->>'universeUUID')::uuid);

-- Update owner_uuid with customer_uuid if universe_uuid is missing from task_parms
UPDATE schedule SET owner_uuid = customer_uuid where owner_uuid is null;

ALTER TABLE schedule ALTER COLUMN schedule_name SET NOT NULL;
ALTER TABLE schedule ALTER COLUMN owner_uuid SET NOT NULL;

ALTER TABLE schedule DROP CONSTRAINT IF EXISTS unique_name_per_owner_per_customer;
ALTER TABLE schedule ADD CONSTRAINT unique_name_per_owner_per_customer UNIQUE (customer_uuid, owner_uuid, schedule_name);
