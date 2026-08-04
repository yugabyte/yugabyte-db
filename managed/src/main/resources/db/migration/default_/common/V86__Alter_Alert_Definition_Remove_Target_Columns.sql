-- Copyright (c) YugaByte, Inc.
insert into alert_definition_label (definition_uuid, name, value)
(select definition_uuid, 'target_uuid', value from alert_definition_label
 where name = 'universe_uuid');

insert into alert_definition_label (definition_uuid, name, value)
(select definition_uuid, 'target_name', value from alert_definition_label
 where name = 'universe_name');

insert into alert_definition_label (definition_uuid, name, value)
(select definition_uuid, 'target_type', 'universe' from alert_definition_label
 where name = 'universe_uuid');

alter table alert_definition drop column if exists target_type;
update alert_definition set config_written = false;