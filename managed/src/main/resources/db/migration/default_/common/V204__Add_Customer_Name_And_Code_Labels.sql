-- Copyright (c) YugaByte, Inc.

insert into alert_definition_label (definition_uuid, name, value)
 (select ad.uuid, 'customer_code', (select code from customer c where ad.customer_uuid = c.uuid)
  from alert_definition ad);
insert into alert_definition_label (definition_uuid, name, value)
 (select ad.uuid, 'customer_name', (select name from customer c where ad.customer_uuid = c.uuid)
  from alert_definition ad);

