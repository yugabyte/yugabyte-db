-- Copyright (c) YugabyteDB, Inc.
alter table alert_definition add column if not exists query_duration_sec integer default 15 not null;
