-- Copyright (c) YugabyteDB, Inc.

alter table universe add column if not exists update_time timestamp;
update universe set update_time = creation_date where update_time is null;
