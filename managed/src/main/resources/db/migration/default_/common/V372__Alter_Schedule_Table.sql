-- Copyright (c) YugaByte, Inc.

alter table if exists schedule add column if not exists use_local_timezone boolean default true not null;