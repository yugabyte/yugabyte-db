-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS node_agent(
  uuid                  UUID NOT NULL,
  customer_uuid         UUID,
  name                  VARCHAR(200),
  ip                    VARCHAR(20),
  version               VARCHAR(20) NOT NULL,
  config                TEXT,
  updated_at            TIMESTAMP NOT NULL,
  CONSTRAINT pk_node_agent primary key (uuid),
  CONSTRAINT fk_node_agent_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE,
  CONSTRAINT uq_node_agent_ip UNIQUE (ip)
)
