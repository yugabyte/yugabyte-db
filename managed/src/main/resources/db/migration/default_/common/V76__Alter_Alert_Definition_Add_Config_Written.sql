-- Copyright (c) YugaByte, Inc.
alter table alert_definition add column if not exists config_written boolean default false not null;
create index if not exists ix_alert_definition_config_written on alert_definition (config_written);
