-- Copyright (c) YugaByte, Inc.

alter table alert_route add column if not exists name varchar(255) not null;
alter table alert_route add column if not exists customer_uuid uuid not null;

alter table alert_route drop column if exists definition_uuid;
alter table alert_route add constraint fk_alert_route_customer_uuid FOREIGN KEY (customer_uuid)
REFERENCES customer(uuid) ON UPDATE CASCADE ON DELETE CASCADE;
