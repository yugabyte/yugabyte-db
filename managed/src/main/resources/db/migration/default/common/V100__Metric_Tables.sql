-- Copyright (c) YugaByte, Inc.
create table if not exists metric (
  uuid                          uuid primary key,
  name                          varchar(4000) not null,
  type                          varchar(12) not null,
  customer_uuid                 uuid,
  create_time                   timestamp not null,
  update_time                   timestamp not null,
  expire_time                   timestamp,
  target_uuid                   uuid,
  value                         double precision not null,
  constraint ck_metric_type check ( type in ('GAUGE')),
  constraint fk_metric_customer_uuid foreign key (customer_uuid) references customer (uuid)
    on delete cascade on update cascade,
  constraint uq_name_customer_target unique (name, customer_uuid, target_uuid)
);

create index if not exists ix_metric_expire_time on metric (expire_time);

create table metric_label (
  metric_uuid                   uuid not null,
  name                          varchar(4000) not null,
  value                         text not null,
  constraint pk_metric_label primary key (metric_uuid, name),
  constraint fk_metric_id foreign key (metric_uuid) references metric (uuid)
    on delete cascade on update cascade
);