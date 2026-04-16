-- Copyright (c) Yugabyte, Inc.

ALTER TABLE customer_config ALTER COLUMN data TYPE binary varying;

ALTER TABLE provider ALTER COLUMN config TYPE binary varying;

ALTER TABLE kms_config ALTER COLUMN auth_config TYPE binary varying;
