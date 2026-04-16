-- Copyright (c) YugaByte, Inc.
create table if not exists alert_definition_group (
  uuid                          uuid primary key,
  customer_uuid                 uuid not null,
  name                          varchar(4000) not null,
  description                   text,
  create_time                   timestamp not null,
  target_type                   varchar(100) not null,
  target                        text not null,
  thresholds                    text not null,
  template                      varchar(100) not null,
  duration_sec                  integer default 15 not null,
  active                        boolean not null,
  route_uuid                    uuid,
  constraint fk_adg_customer_uuid foreign key (customer_uuid) references customer (uuid)
    on delete cascade on update cascade,
  constraint fk_adg_route_uuid foreign key (route_uuid) references alert_route (uuid)
    on delete restrict on update restrict,
  constraint ck_adg_target_type check (target_type in ('CUSTOMER','UNIVERSE'))
);

alter table alert_definition add column if not exists group_uuid uuid;
alter table alert_definition add constraint fk_ad_group_uuid foreign key (group_uuid) references alert_definition_group (uuid)
  on delete cascade on update cascade;