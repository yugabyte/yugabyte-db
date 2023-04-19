-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS alert_template_variable(
  uuid                  UUID NOT NULL,
  customer_uuid         UUID NOT NULL,
  name                  VARCHAR(400),
  possible_values       TEXT,
  default_value         TEXT,
  CONSTRAINT pk_alert_template_variable primary key (uuid),
  CONSTRAINT fk_atv_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE,
  CONSTRAINT uq_atv_customer_name UNIQUE (customer_uuid, name)
)
