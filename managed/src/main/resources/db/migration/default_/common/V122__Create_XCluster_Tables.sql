-- Copyright (c) YugaByte, Inc.

create table if not exists xcluster_config (
    uuid                            uuid,
    config_name                     varchar(256) not null,
    source_universe_uuid            uuid not null,
    target_universe_uuid            uuid not null,
    status                          varchar(32) not null,
    create_time                     timestamp not null,
    modify_time                     timestamp not null,
    constraint pk_xcluster_config primary key (uuid),
    constraint uq_xcluster_config_source_universe_uuid_target_universe_uuid unique (source_universe_uuid, target_universe_uuid),
    constraint fk_xcluster_config_source_universe_uuid foreign key (source_universe_uuid) references universe (universe_uuid) on delete cascade on update cascade,
    constraint fk_xcluster_config_target_universe_uuid foreign key (target_universe_uuid) references universe (universe_uuid) on delete cascade on update cascade,
    constraint ck_xcluster_config_status check (status in ('Init','Running','Paused','Failed'))
);

create table if not exists xcluster_table_config (
    config_uuid                     uuid not null,
    table_id                        varchar(64) not null,
    constraint pk_xcluster_config_table primary key (config_uuid, table_id),
    constraint fk_xcluster_config_table_config_uuid foreign key (config_uuid) references xcluster_config (uuid) on delete cascade on update cascade
);
