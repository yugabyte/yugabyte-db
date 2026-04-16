-- Copyright (c) YugaByte, Inc.
create table if not exists alert_definition_label (
  definition_uuid               uuid not null,
  name                          VARCHAR(4000) not null,
  value                         TEXT not null,
  constraint pk_alert_definition_label primary key (definition_uuid, name),
  constraint fk_definition_uuid foreign key (definition_uuid) references alert_definition (uuid)
    on delete cascade on update cascade
);
