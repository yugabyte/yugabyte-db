-- Copyright (c) YugaByte, Inc.
create table if not exists custom_ca_certificate_info (
  id                            uuid not null,
  customer_id                   uuid not null,
  name                          varchar(255) not null,
  start_date                    timestamp not null,
  expiry_date                   timestamp not null,
  created_time                  timestamp not null,
  contents                      TEXT not null,
  active                        boolean default false not null,
  constraint pk_custom_cacert primary key (id),
  constraint fk_custom_cacert_customer_id foreign key (customer_id) references customer (uuid)
    on delete cascade on update cascade
);

-- ui display use case
create index if not exists ix_custom_cacert_created on custom_ca_certificate_info (customer_id, created_time);

-- expiry alert use case
create index if not exists ix_custom_cacert_expiry on custom_ca_certificate_info (customer_id, expiry_date);

