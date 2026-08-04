-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS role_binding (
  uuid uuid NOT NULL,
  user_uuid uuid NOT NULL,
  type VARCHAR(64) NOT NULL,
  role_uuid uuid NOT NULL,
  create_time TIMESTAMP NOT NULL,
  update_time TIMESTAMP NOT NULL,
  resource_group JSON_ALIAS,
  CONSTRAINT pk_role_binding PRIMARY KEY (uuid),
  CONSTRAINT ck_role_binding_type CHECK (type in ('System','Custom')),
  CONSTRAINT fk_role_binding_user_uuid FOREIGN KEY (user_uuid) REFERENCES users(uuid) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT fk_role_binding_role_uuid FOREIGN KEY (role_uuid) REFERENCES role(role_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);