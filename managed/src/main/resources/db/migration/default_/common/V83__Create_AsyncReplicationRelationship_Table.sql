-- Copyright (c) YugaByte, Inc.

create table if not exists async_replication_relationship (
    uuid                            uuid primary key,
    source_universe_uuid            uuid not null references universe (universe_uuid) on delete cascade on update cascade,
    source_table_uuid               uuid not null,
    target_universe_uuid            uuid not null references universe (universe_uuid) on delete cascade on update cascade,
    target_table_uuid               uuid not null,
    active                          boolean not null
);
