# --- Created by Ebean DDL
# To stop Ebean DDL generation, remove this comment and start using Evolutions

# --- !Ups

create table availability_zone (
  uuid                          varchar(40) not null,
  code                          varchar(25) not null,
  name                          varchar(100) not null,
  region_uuid                   varchar(40),
  active                        boolean default true not null,
  subnet                        varchar(50) not null,
  constraint pk_availability_zone primary key (uuid)
);

create table customer (
  uuid                          varchar(40) not null,
  email                         varchar(256) not null,
  password_hash                 varchar(256) not null,
  name                          varchar(256) not null,
  creation_date                 datetime(6) not null,
  auth_token                    varchar(255),
  auth_token_issue_date         datetime(6),
  constraint uq_customer_email unique (email),
  constraint pk_customer primary key (uuid)
);

create table customer_task (
  id                            bigint auto_increment not null,
  customer_uuid                 varchar(40),
  task_uuid                     varchar(40) not null,
  target_type                   varchar(8) not null,
  target_name                   varchar(255) not null,
  type                          varchar(6) not null,
  create_time                   datetime(6) not null,
  completion_time               datetime(6),
  constraint ck_customer_task_target_type check (target_type in ('Table','Instance')),
  constraint ck_customer_task_type check (type in ('Delete','Create','Update')),
  constraint pk_customer_task primary key (id)
);

create table instance (
  instance_id                   varchar(40) not null,
  customer_id                   varchar(40) not null,
  name                          varchar(255) not null,
  customer_uuid                 varchar(40),
  placement_info                longtext not null,
  creation_date                 datetime(6) not null,
  constraint pk_instance primary key (instance_id,customer_id)
);

create table instance_info (
  instance_uuid                 varchar(40) not null,
  instance_details              longtext not null,
  constraint pk_instance_info primary key (instance_uuid)
);

create table provider (
  uuid                          varchar(40) not null,
  name                          varchar(255) not null,
  active                        boolean default true not null,
  constraint uq_provider_name unique (name),
  constraint pk_provider primary key (uuid)
);

create table region (
  uuid                          varchar(40) not null,
  code                          varchar(25) not null,
  name                          varchar(100) not null,
  longitude                     double,
  latitude                      double,
  provider_uuid                 varchar(40),
  active                        boolean default true not null,
  constraint pk_region primary key (uuid)
);

create table task_info (
  uuid                          varchar(40) not null,
  task_type                     varchar(15) not null,
  task_state                    varchar(7) not null,
  percent_done                  integer default 0,
  details                       longtext not null,
  owner                         varchar(255) not null,
  create_time                   datetime(6) not null,
  update_time                   datetime(6) not null,
  constraint ck_task_info_task_type check (task_type in ('DestroyInstance','CreateInstance')),
  constraint ck_task_info_task_state check (task_state in ('Running','Success','Failure','Created')),
  constraint pk_task_info primary key (uuid)
);

alter table availability_zone add constraint fk_availability_zone_region_uuid foreign key (region_uuid) references region (uuid) on delete restrict on update restrict;
create index ix_availability_zone_region_uuid on availability_zone (region_uuid);

alter table customer_task add constraint fk_customer_task_customer_uuid foreign key (customer_uuid) references customer (uuid) on delete restrict on update restrict;
create index ix_customer_task_customer_uuid on customer_task (customer_uuid);

alter table instance add constraint fk_instance_customer_uuid foreign key (customer_uuid) references customer (uuid) on delete restrict on update restrict;
create index ix_instance_customer_uuid on instance (customer_uuid);

alter table region add constraint fk_region_provider_uuid foreign key (provider_uuid) references provider (uuid) on delete restrict on update restrict;
create index ix_region_provider_uuid on region (provider_uuid);


# --- !Downs

alter table availability_zone drop foreign key fk_availability_zone_region_uuid;
drop index ix_availability_zone_region_uuid on availability_zone;

alter table customer_task drop foreign key fk_customer_task_customer_uuid;
drop index ix_customer_task_customer_uuid on customer_task;

alter table instance drop foreign key fk_instance_customer_uuid;
drop index ix_instance_customer_uuid on instance;

alter table region drop foreign key fk_region_provider_uuid;
drop index ix_region_provider_uuid on region;

drop table if exists availability_zone;

drop table if exists customer;

drop table if exists customer_task;

drop table if exists instance;

drop table if exists instance_info;

drop table if exists provider;

drop table if exists region;

drop table if exists task_info;

