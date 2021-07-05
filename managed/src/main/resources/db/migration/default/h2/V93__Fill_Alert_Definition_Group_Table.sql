-- Copyright (c) YugaByte, Inc.

-- No need to fill anything for h2.
alter table alert_definition drop column if exists name;
alter table alert_definition drop column if exists query_duration_sec;
alter table alert_definition drop column if exists query_threshold;
alter table alert_definition drop column if exists active;
alter table alert_definition alter column group_uuid set not null;