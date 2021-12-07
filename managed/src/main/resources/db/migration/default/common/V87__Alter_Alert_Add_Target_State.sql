-- Copyright (c) YugaByte, Inc.
alter table alert add column if not exists target_state varchar(8);
alter table alert add constraint ck_alert_target_state check ( target_state in ('CREATED','ACTIVE','RESOLVED'));
update alert set target_state = state;
update alert set target_state = 'ACTIVE' where state = 'CREATED';
alter table alert alter column target_state set not null;