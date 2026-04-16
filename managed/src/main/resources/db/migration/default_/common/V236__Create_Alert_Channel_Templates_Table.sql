-- Copyright (c) YugaByte, Inc.
CREATE TABLE IF NOT EXISTS alert_channel_templates (
  type                  VARCHAR(25) NOT NULL,
  customer_uuid         UUID NOT NULL,
  title_template        TEXT,
  text_template         TEXT NOT NULL,
  CONSTRAINT pk_alert_channel_templates PRIMARY KEY (type, customer_uuid),
  CONSTRAINT ck_act_channel_type CHECK (type in ('Email','Slack','PagerDuty','WebHook')),
  CONSTRAINT fk_act_customer_uuid foreign key (customer_uuid) references customer (uuid) on delete cascade on update cascade
);
