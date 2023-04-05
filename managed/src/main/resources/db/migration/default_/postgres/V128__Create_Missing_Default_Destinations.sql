-- Copyright (c) YugaByte, Inc.

DO $$
DECLARE
  row record;
BEGIN

  FOR row IN
    SELECT uuid,
           gen_random_uuid() AS default_destination_uuid,
           gen_random_uuid() AS default_channel_uuid FROM customer
  LOOP
    -- We are going to create default destination and channel if no default destination
    -- exists for the customer (Bug in V94 migration)
    IF NOT EXISTS (SELECT uuid FROM alert_destination
                     WHERE default_destination = true AND customer_uuid = row.uuid) THEN

      INSERT INTO alert_destination (uuid, customer_uuid, name, default_destination)
        VALUES (row.default_destination_uuid, row.uuid, 'Default Destination', true);
      INSERT INTO alert_channel (uuid, customer_uuid, name, params)
        (SELECT row.default_channel_uuid, row.uuid, 'Default Channel',
         '{"channelType":"Email","titleTemplate":null,"textTemplate":null,"defaultRecipients":"true","recipients":[],"smtpData":null, "defaultSmtpSettings":"true"}');

      INSERT INTO alert_destination_group (destination_uuid, channel_uuid)
        VALUES (row.default_destination_uuid, row.default_channel_uuid);
    END IF;
  END LOOP;
END $$;

