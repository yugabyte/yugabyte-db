-- Copyright (c) YugaByte, Inc.

UPDATE alert_definition
  SET query = replace(query, 'ybp_alert_manager_receiver_status', 'ybp_alert_manager_channel_status')
  WHERE query like '%ybp_alert_manager_receiver_status%';

ALTER TABLE alert_route RENAME COLUMN default_route TO default_destination;
ALTER TABLE alert_route RENAME TO alert_destination;
ALTER TABLE alert_destination DROP CONSTRAINT IF EXISTS uq_customer_uuid_route_name;
ALTER TABLE alert_destination ADD CONSTRAINT
  uq_customer_uuid_destination_name unique (customer_uuid, name);
ALTER INDEX IF EXISTS pk_alert_route RENAME TO pk_alert_destination;

ALTER TABLE alert_route_group RENAME COLUMN route_uuid TO destination_uuid;
ALTER TABLE alert_route_group RENAME COLUMN receiver_uuid TO channel_uuid;
ALTER TABLE alert_route_group RENAME TO alert_destination_group;
ALTER TABLE alert_destination_group RENAME CONSTRAINT fk_alert_route_group_receiver_uuid
  TO fk_alert_destination_group_channel_uuid;
ALTER TABLE alert_destination_group RENAME CONSTRAINT fk_alert_route_group_route_uuid
  TO fk_alert_destination_group_destination_uuid;
ALTER INDEX IF EXISTS pk_alert_route_group RENAME TO pk_alert_destination_group;

UPDATE alert_receiver SET params = replace(params::text, '"targetType":', '"channelType":')::json_alias;
ALTER TABLE alert_receiver RENAME TO alert_channel;
ALTER TABLE alert_channel DROP CONSTRAINT IF EXISTS uq_customer_uuid_receiver_name;
ALTER TABLE alert_channel ADD CONSTRAINT
  uq_customer_uuid_channel_name unique (customer_uuid, name);
ALTER TABLE alert_channel RENAME CONSTRAINT fk_ar_customer_uuid
  TO fk_alert_channel_customer_uuid;
ALTER INDEX IF EXISTS pk_alert_receiver RENAME TO pk_alert_channel;

UPDATE alert_destination SET name = 'Default Destination' WHERE name = 'Default Route';
UPDATE alert_channel SET name = 'Default Channel' WHERE name = 'Default Receiver';
