-- Copyright (c) YugaByte, Inc.

alter table alert_definition_group add column if not exists threshold_unit text;
alter table alert_definition_group add constraint ck_adg_threshold_unit check (threshold_unit in ('PERCENT','MILLISECOND'));
update alert_definition_group set threshold_unit = 'PERCENT' where name = 'Memory Consumption';
update alert_definition_group set threshold_unit = 'MILLISECOND' where name in ('Replication Lag', 'Clock Skew');
alter table alert_definition_group alter column threshold_unit set not null;
