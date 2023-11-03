-- Copyright (c) YugaByte, Inc.

update customer_config set config_name = concat(name, '-Default') where config_name is null;
alter table customer_config alter column config_name SET not null;
