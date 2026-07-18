-- Copyright (c) YugaByte, Inc.

ALTER TABLE role_binding
DROP CONSTRAINT fk_role_binding_user_uuid;

ALTER TABLE role_binding
RENAME COLUMN user_uuid TO principal_uuid;

ALTER TABLE role_binding
ADD CONSTRAINT fk_role_binding_user_uuid  FOREIGN KEY (principal_uuid) REFERENCES principal (uuid);