-- Copyright (c) YugaByte, Inc.
alter table alert_route add column if not exists default_route boolean default false;

DO $$
DECLARE
  row record;
  splitted_recipients text[];
  singlevalue text;
  default_recipients text[];
BEGIN

  -- Adding unique key for names in alert_route.
  IF NOT EXISTS (select constraint_name 
                 from information_schema.table_constraints 
                 where table_name='alert_route' and constraint_name='uq_customer_uuid_route_name') THEN
      ALTER TABLE alert_route ADD CONSTRAINT uq_customer_uuid_route_name unique (customer_uuid, name);
  END IF;

  -- Adding unique key for names in alert_receiver.
  IF NOT EXISTS (select constraint_name 
                 from information_schema.table_constraints 
                 where table_name='alert_receiver' and constraint_name='uq_customer_uuid_receiver_name') THEN
      ALTER TABLE alert_receiver ADD CONSTRAINT uq_customer_uuid_receiver_name unique (customer_uuid, name);
  END IF;

  FOR row IN
    SELECT uuid,
           gen_random_uuid() as default_route_uuid,
           gen_random_uuid() as default_receiver_uuid FROM customer
  LOOP
    -- We are going to create default route and receiver if there is no default route already exist.
    IF NOT EXISTS (select uuid from alert_route where default_route = true) THEN

      INSERT INTO alert_route (uuid, customer_uuid, name, default_route) values (row.default_route_uuid, row.uuid, 'Default Route', true);
      INSERT INTO alert_receiver (uuid, customer_uuid, name, params) (select row.default_receiver_uuid, row.uuid, 'Default Receiver',
         '{"targetType":"Email","titleTemplate":null,"textTemplate":null,"defaultRecipients":"true","recipients":[],"smtpData":null, "defaultSmtpSettings":"true"}');

      INSERT INTO alert_route_group (route_uuid, receiver_uuid) values (row.default_route_uuid, row.default_receiver_uuid);
    END IF;
  END LOOP;
END $$;
