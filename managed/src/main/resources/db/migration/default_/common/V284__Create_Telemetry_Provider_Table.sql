-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS telemetry_provider (
  uuid                     uuid NOT NULL,
  customer_uuid            uuid,
  type                     VARCHAR(255),
  name                     VARCHAR(255),
  create_time              timestamp,
  update_time              timestamp,
  config                   JSON_ALIAS,
  tags                     JSON_ALIAS,
  CONSTRAINT pk_telemetry_provider PRIMARY KEY (uuid),
  CONSTRAINT fk_telemetry_provider_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE
);
