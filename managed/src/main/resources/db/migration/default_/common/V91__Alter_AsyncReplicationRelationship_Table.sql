-- Copyright (c) YugaByte, Inc.

alter table if exists async_replication_relationship alter source_table_uuid type varchar;
alter table if exists async_replication_relationship alter target_table_uuid type varchar;

alter table if exists async_replication_relationship add constraint unique_relationship
    unique (source_universe_uuid, source_table_uuid, target_universe_uuid, target_table_uuid);

alter table if exists async_replication_relationship rename column source_table_uuid to source_table_id;
alter table if exists async_replication_relationship rename column target_table_uuid to target_table_id;
