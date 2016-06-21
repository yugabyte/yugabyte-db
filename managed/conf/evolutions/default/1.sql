# --- Created by Ebean DDL
# To stop Ebean DDL generation, remove this comment and start using Evolutions

# --- !Ups

create table availability_zone (
  uuid                          uuid not null,
  code                          varchar(25) not null,
  name                          varchar(100) not null,
  region_uuid                   uuid,
  active                        boolean default true not null,
  subnet                        varchar(50) not null,
  constraint pk_availability_zone primary key (uuid)
);

create table customer (
  uuid                          uuid not null,
  email                         varchar(256) not null,
  password_hash                 varchar(256) not null,
  name                          varchar(256) not null,
  creation_date                 timestamp not null,
  auth_token                    varchar(255),
  auth_token_issue_date         timestamp,
  constraint uq_customer_email unique (email),
  constraint pk_customer primary key (uuid)
);

create table instance (
  instance_id                   uuid not null,
  customer_id                   uuid not null,
  name                          varchar(255) not null,
  customer_uuid                 uuid,
  placement_info                clob not null,
  provisioning_state            varchar(10) not null,
  creation_date                 timestamp not null,
  constraint ck_instance_provisioning_state check (provisioning_state in ('Failed','Completed','Pending','Processing')),
  constraint pk_instance primary key (instance_id,customer_id)
);

create table instance_info (
  instance_uuid                 uuid not null,
  instance_details              clob not null,
  constraint pk_instance_info primary key (instance_uuid)
);

create table provider (
  uuid                          uuid not null,
  name                          varchar(255) not null,
  active                        boolean default true not null,
  constraint uq_provider_name unique (name),
  constraint pk_provider primary key (uuid)
);

create table region (
  uuid                          uuid not null,
  code                          varchar(25) not null,
  name                          varchar(100) not null,
  provider_uuid                 uuid,
  active                        boolean default true not null,
  multi_az_capable              boolean default true not null,
  constraint pk_region primary key (uuid)
);

create table task_info (
  uuid                          uuid not null,
  task_type                     varchar(15) not null,
  task_state                    varchar(7) not null,
  percent_done                  integer default 0,
  details                       clob not null,
  owner                         varchar(255) not null,
  create_time                   timestamp not null,
  update_time                   timestamp not null,
  constraint ck_task_info_task_type check (task_type in ('DestroyInstance','CreateInstance')),
  constraint ck_task_info_task_state check (task_state in ('Running','Success','Failure','Created')),
  constraint pk_task_info primary key (uuid)
);

alter table availability_zone add constraint fk_availability_zone_region_uuid foreign key (region_uuid) references region (uuid) on delete restrict on update restrict;
create index ix_availability_zone_region_uuid on availability_zone (region_uuid);

alter table instance add constraint fk_instance_customer_uuid foreign key (customer_uuid) references customer (uuid) on delete restrict on update restrict;
create index ix_instance_customer_uuid on instance (customer_uuid);

alter table region add constraint fk_region_provider_uuid foreign key (provider_uuid) references provider (uuid) on delete restrict on update restrict;
create index ix_region_provider_uuid on region (provider_uuid);


# --- !Downs

alter table availability_zone drop constraint if exists fk_availability_zone_region_uuid;
drop index if exists ix_availability_zone_region_uuid;

alter table instance drop constraint if exists fk_instance_customer_uuid;
drop index if exists ix_instance_customer_uuid;

alter table region drop constraint if exists fk_region_provider_uuid;
drop index if exists ix_region_provider_uuid;

drop table if exists availability_zone;

drop table if exists customer;

drop table if exists instance;

drop table if exists instance_info;

drop table if exists provider;

drop table if exists region;

drop table if exists task_info;

