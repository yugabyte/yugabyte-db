-- Copyright (c) YugaByte, Inc.

alter table if exists xcluster_table_config add column if not exists stream_id varchar(64);
