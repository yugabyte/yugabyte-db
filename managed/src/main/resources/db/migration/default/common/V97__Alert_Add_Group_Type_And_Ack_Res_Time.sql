-- Copyright (c) YugaByte, Inc.
alter table alert add column if not exists group_type varchar(100);
alter table alert add constraint ck_alert_group_type check ( group_type in ('CUSTOMER','UNIVERSE'));
update alert set group_type = 'UNIVERSE' where group_uuid is not null;

alter table alert add column if not exists acknowledged_time timestamp;
alter table alert add column if not exists resolved_time timestamp;