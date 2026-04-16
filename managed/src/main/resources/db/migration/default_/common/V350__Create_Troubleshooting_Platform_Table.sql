-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS troubleshooting_platform (
  uuid                     uuid NOT NULL,
  customer_uuid            uuid NOT NULL,
  tp_url                   text NOT NULL,
  yba_url                  text NOT NULL,
  metrics_url              text NOT NULL,
  CONSTRAINT pk_troubleshooting_platform PRIMARY KEY (uuid),
  CONSTRAINT fk_troubleshooting_platform_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE
);
