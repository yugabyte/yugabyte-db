-- Copyright (c) YugaByte, Inc.

create table if not exists alert_receiver (
  uuid                          uuid not null,
  customer_uuid                 uuid not null,
  target_type                   varchar(25) not null,
  params                        JSON_ALIAS not null,
  constraint ck_alert_receiver_target_type check (target_type in ('Email','Slack','Sms','PagerDuty')),
  constraint pk_alert_receiver primary key (uuid),
  constraint fk_ar_customer_uuid foreign key (customer_uuid) references customer (uuid) ON UPDATE CASCADE ON DELETE CASCADE
);

create table if not exists alert_route (
  uuid                          uuid not null,
  definition_uuid               uuid not null,
  receiver_uuid                 uuid not null,
  constraint pk_alert_route primary key (uuid),
  constraint fk_alert_receiver_uuid foreign key (receiver_uuid) references alert_receiver (uuid) ON DELETE CASCADE ON UPDATE CASCADE,
  constraint fk_alert_definition_uuid foreign key (definition_uuid) references alert_definition (uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
