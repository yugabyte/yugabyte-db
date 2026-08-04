 -- Copyright (c) YugaByte, Inc.

ALTER TABLE role_binding
DROP CONSTRAINT fk_role_binding_user_uuid;

ALTER TABLE role_binding
ADD CONSTRAINT fk_role_binding_principal_uuid  FOREIGN KEY (principal_uuid) REFERENCES principal (uuid) ON UPDATE CASCADE ON DELETE CASCADE;
 
ALTER TABLE users ADD COLUMN if not exists group_memberships UUID ARRAY;