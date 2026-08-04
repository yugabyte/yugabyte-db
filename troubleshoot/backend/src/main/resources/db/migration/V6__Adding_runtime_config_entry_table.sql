CREATE TABLE runtime_config_entry (
  scope_uuid                UUID NOT NULL,
  path                      VARCHAR(127) CHECK (path <> ''),
  value                     VARCHAR(255),

  -- scope_uuid is part of primary key and a foreign key
  CONSTRAINT pk_runtime_config_entry PRIMARY KEY (scope_uuid, path)
);
