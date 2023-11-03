-- Copyright (c) YugaByte, Inc.

alter table alert add column if not exists name varchar(4000);
update alert set name = (select value from alert_label
 where alert_uuid = alert.uuid and name = 'definition_name');

alter table alert add column if not exists target_name varchar(4000);
update alert set target_name = (select value from alert_label
 where alert_uuid = alert.uuid and name = 'target_name');
