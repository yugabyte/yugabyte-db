-- Copyright (c) YugaByte, Inc.

create table if not exists high_availability_config (
    uuid                            uuid not null,
    cluster_key                     varchar(44) not null unique,
    constraint pk_high_availability_config primary key (uuid)
);

create table if not exists platform_instance (
    uuid                           uuid not null,
    address                        varchar(64) not null unique,
    config_uuid                    uuid not null,
    last_backup                    timestamp,
    is_leader                      boolean default null unique,
    is_local                       boolean default null unique,
    constraint pk_platform_instance primary key (uuid),
    constraint fk_platform_instance_high_availability_config_1 foreign key (config_uuid) references high_availability_config (uuid) on delete cascade on update cascade
);
