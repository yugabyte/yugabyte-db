-- Copyright (c) YugaByte, Inc.
alter table alert_definition add column if not exists target_type varchar(20);
update alert_definition set target_type = 'Universe' where target_type is null ;
alter table alert_definition alter column target_type set not null;
