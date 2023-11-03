-- Copyright (c) YugaByte, Inc.

alter table runtime_config_entry alter column value set data type text;

-- No need to fill anything for h2.
alter table alert_definition drop column if exists name;
alter table alert_definition drop column if exists query_duration_sec;
alter table alert_definition drop column if exists query_threshold;
alter table alert_definition drop column if exists active;
alter table alert_definition alter column group_uuid set not null;
