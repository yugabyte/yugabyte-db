-- Copyright (c) YugaByte, Inc.

create table if not exists alert_route_group (
  route_uuid                    uuid not null,
  receiver_uuid                 uuid not null,
  constraint pk_alert_route_group primary key (route_uuid, receiver_uuid),
  constraint fk_alert_route_group_route_uuid foreign key (route_uuid)
    references alert_route (uuid) on delete cascade on update cascade,
  constraint fk_alert_route_group_receiver_uuid foreign key (receiver_uuid)
    references alert_receiver (uuid) on delete cascade on update cascade
);

DO
$$
  BEGIN
    -- Look for our column
    IF EXISTS (select column_name 
               from information_schema.columns 
               where table_name='alert_route' and column_name='receiver_uuid') THEN
        EXECUTE 'INSERT INTO alert_route_group (route_uuid, receiver_uuid) SELECT uuid, receiver_uuid FROM alert_route ON CONFLICT DO NOTHING;';
    END IF;
  END;
$$;

ALTER TABLE alert_route DROP COLUMN IF EXISTS receiver_uuid;
