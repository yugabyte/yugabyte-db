-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS role (
  role_uuid uuid NOT NULL,
  customer_uuid uuid NOT NULL,
  name VARCHAR(255) NOT NULL,
  created_on TIMESTAMP NOT NULL,
  updated_on TIMESTAMP NOT NULL,
  role_type VARCHAR(64) NOT NULL,
  permission_details JSON_ALIAS,
  CONSTRAINT pk_role PRIMARY KEY (role_uuid),
  CONSTRAINT ck_role_role_type CHECK (role_type in ('System','Custom')),
  CONSTRAINT fk_role_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
