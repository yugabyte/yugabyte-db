-- Copyright (c) YugaByte, Inc.
alter table universe add column swamper_config_written boolean default true not null;
-- Need to write initial updated config
update universe set swamper_config_written = false;
