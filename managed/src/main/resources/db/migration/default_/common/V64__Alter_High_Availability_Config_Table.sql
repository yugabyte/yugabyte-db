-- Copyright (c) YugaByte, Inc.

alter table if exists high_availability_config add column id integer not null default 1;
alter table if exists high_availability_config add constraint one_row check(id = 1);
alter table if exists high_availability_config add constraint unique_id unique (id);
