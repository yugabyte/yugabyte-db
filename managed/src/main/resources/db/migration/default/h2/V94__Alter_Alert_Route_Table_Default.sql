-- Copyright (c) YugaByte, Inc.

ALTER TABLE alert_route ADD COLUMN IF NOT EXISTS default_route boolean default false;
ALTER TABLE alert_route ADD CONSTRAINT IF NOT EXISTS unique_route_name_per_customer UNIQUE (customer_uuid, name);
ALTER TABLE alert_receiver ADD CONSTRAINT IF NOT EXISTS unique_receiver_name_per_customer UNIQUE (customer_uuid, name);

--- No actions to generate default route/receiver is required. 
