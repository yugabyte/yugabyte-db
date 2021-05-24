-- Copyright (c) YugaByte, Inc.

alter table customer_config add column if not exists config_name varchar(64) null;
update customer_config set config_name = concat(name, '-Default') where config_name is null;
