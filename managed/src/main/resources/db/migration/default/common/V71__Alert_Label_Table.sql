-- Copyright (c) YugaByte, Inc.
create table if not exists alert_label (
  alert_uuid                    uuid not null,
  name                          VARCHAR(4000) not null,
  value                         TEXT not null,
  constraint pk_alert_label primary key (alert_uuid, name),
  constraint fk_alert_uuid foreign key (alert_uuid) references alert (uuid)
    on delete cascade on update cascade
);
