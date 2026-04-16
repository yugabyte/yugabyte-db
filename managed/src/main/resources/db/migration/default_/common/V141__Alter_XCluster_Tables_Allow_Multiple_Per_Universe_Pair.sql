-- Copyright (c) YugaByte, Inc.

alter table if exists xcluster_config
  drop constraint if exists uq_xcluster_config_source_universe_uuid_target_universe_uuid;

alter table if exists xcluster_config
  drop constraint if exists uq_xcluster_config_name_source_target;

alter table if exists xcluster_config
  add constraint uq_xcluster_config_name_source_target
    unique (config_name, source_universe_uuid, target_universe_uuid);

alter table if exists xcluster_config
  drop constraint if exists ck_xcluster_config_status;

alter table if exists xcluster_config
  add constraint ck_xcluster_config_status
    check (status in ('Init','Running','Updating','Paused','Failed'));


