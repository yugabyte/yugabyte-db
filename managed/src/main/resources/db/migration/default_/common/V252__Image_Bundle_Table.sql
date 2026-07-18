-- Copyright (c) YugaByte, Inc.

create table if not exists image_bundle (
  uuid                          uuid not null,
  name                          varchar(255) not null,
  provider_uuid                 uuid not null,
  details                       json_alias,
  is_default                    boolean default false,
  constraint pk_image_bundle primary key (uuid, name),
  constraint fk_image_bundle_provider_uuid foreign key (provider_uuid) references provider (uuid) on delete restrict on update restrict
);
