-- Copyright (c) YugaByte, Inc.
alter table alert_definition add column if not exists version bigint default 0 not null;
