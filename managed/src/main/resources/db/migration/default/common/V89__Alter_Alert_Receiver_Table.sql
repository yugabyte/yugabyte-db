-- Copyright (c) YugaByte, Inc.

alter table alert_receiver add column if not exists name varchar(255);
update alert_receiver set name = uuid;
alter table alert_receiver alter column name set not null;
