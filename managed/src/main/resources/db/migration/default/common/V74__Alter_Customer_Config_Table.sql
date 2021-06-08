-- Copyright (c) YugaByte, Inc.

alter table customer_config add column if not exists config_name varchar(100) null;
update customer_config set config_name = concat(name, '-Default') where config_name is null;
create index if not exists unique_config_name on customer_config (config_uuid, name, config_name);
