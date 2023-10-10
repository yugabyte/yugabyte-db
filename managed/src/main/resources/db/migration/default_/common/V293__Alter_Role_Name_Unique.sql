-- Copyright (c) YugaByte, Inc.

ALTER TABLE role ADD CONSTRAINT uq_role_name_customer UNIQUE (name, customer_uuid);
