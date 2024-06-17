-- Copyright (c) YugaByte, Inc.

ALTER TABLE IF EXISTS xcluster_config
    DROP CONSTRAINT IF EXISTS ck_xcluster_config_type;

ALTER TABLE IF EXISTS xcluster_config
    ADD CONSTRAINT ck_xcluster_config_type
    CHECK (type IN (
        'Basic',
        'Txn',
        'Db'
    ));


CREATE TABLE IF not exists xcluster_namespace_config (
  config_uuid                     uuid not null,
  source_namespace_id             varchar(64) not null,
  constraint pk_xcluster_namespace_config primary key (config_uuid, source_namespace_id),
  constraint fk_xcluster_namespace_config_config_uuid foreign key (config_uuid) references xcluster_config (uuid) on delete cascade on update cascade
);