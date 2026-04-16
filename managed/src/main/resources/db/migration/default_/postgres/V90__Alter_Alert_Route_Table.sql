-- Copyright (c) YugaByte, Inc.

alter table alert_route add column if not exists name varchar(255);
update alert_route set name = uuid;
alter table alert_route alter column name set not null;

alter table alert_route add column if not exists customer_uuid uuid;
update alert_route set customer_uuid=(select customer_uuid from alert_definition where uuid=definition_uuid);
alter table alert_route alter column customer_uuid set not null;

alter table alert_route drop column if exists definition_uuid;
DO
$$
  BEGIN
    -- Look for our constraint
    IF NOT EXISTS (select constraint_name 
                   from information_schema.table_constraints 
                   where table_name='alert_route' and constraint_name='fk_customer_uuid') THEN
        EXECUTE 'alter table alert_route add constraint fk_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON UPDATE CASCADE ON DELETE CASCADE;';
    END IF;
  END;
$$;
