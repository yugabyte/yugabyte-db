-- Copyright (c) YugaByte, Inc. 

CREATE TABLE IF NOT EXISTS restore (
  restore_uuid          UUID NOT NULL,
  universe_uuid         UUID NOT NULL,
  customer_uuid         UUID NOT NULL,
  task_uuid             UUID NOT NULL UNIQUE,
  source_universe_uuid  UUID,
  storage_config_uuid   UUID,
  source_universe_name  varchar(255),
  create_time           timestamp NOT NULL,
  update_time           timestamp,
  restore_size_in_bytes int8,
  state                 varchar(20) NOT NULL,
  CONSTRAINT pk_restore PRIMARY KEY (restore_uuid)
);
CREATE TABLE IF NOT EXISTS restore_keyspace (
  uuid                  UUID NOT NULL,
  restore_uuid          UUID NOT NULL,
  task_uuid             UUID NOT NULL UNIQUE,
  source_keyspace       varchar(255),
  target_keyspace       varchar(255),
  storage_location      text,
  create_time           timestamp NOT NULL,
  complete_time         timestamp,
  state                 varchar(20) NOT NULL,
  CONSTRAINT pk_restore_keyspace PRIMARY KEY (uuid),
  CONSTRAINT fk_restore_keyspace_restore_uuid foreign key (restore_uuid) references restore (restore_uuid) on delete cascade on update cascade
);
