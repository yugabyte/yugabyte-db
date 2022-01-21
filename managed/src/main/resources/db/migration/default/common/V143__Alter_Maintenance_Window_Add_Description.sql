-- Copyright (c) YugaByte, Inc.

alter table maintenance_window
  add column if not exists description text;
