-- Copyright (c) YugaByte, Inc.
alter table alert add column if not exists group_uuid uuid;
alter table alert add constraint fk_alert_group_uuid foreign key (group_uuid) references alert_definition_group (uuid)
  on delete cascade on update cascade;
update alert set group_uuid =
  (select value::uuid from alert_label where alert_uuid = uuid and name = 'group_uuid');

alter table alert add column if not exists severity varchar(100);
alter table alert add constraint ck_alert_severity check (severity in ('SEVERE','WARNING'));
update alert set severity =
  (select value from alert_label where alert_uuid = uuid and name = 'severity');
update alert set severity = 'WARNING' where type = 'Warning' and severity is null;
update alert set severity = 'SEVERE' where severity is null;
alter table alert alter column severity set not null;

alter table alert alter column state type varchar(20);
alter table alert drop constraint if exists ck_alert_state;
alter table alert add constraint ck_alert_state check ( state in ('CREATED','ACTIVE','ACKNOWLEDGED','RESOLVED'));

alter table alert alter column target_state type varchar(20);
alter table alert drop constraint if exists  ck_alert_target_state;
alter table alert add constraint ck_alert_target_state check ( target_state in ('CREATED','ACTIVE','ACKNOWLEDGED','RESOLVED'));

alter table alert drop column if exists type;