-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS hook_scope (
  uuid                  UUID NOT NULL,
  customer_uuid         UUID NOT NULL,
  trigger_type          VARCHAR(64) NOT NULL,
  provider_uuid         UUID,
  universe_uuid         UUID,
  CONSTRAINT pk_hook_scope PRIMARY KEY (uuid),
  CONSTRAINT fk_hook_scope_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT fk_hook_scope_universe_uuid FOREIGN KEY (universe_uuid) REFERENCES universe(universe_uuid) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT fk_hook_scope_provider_uuid FOREIGN KEY (provider_uuid) REFERENCES provider(uuid) ON DELETE CASCADE ON UPDATE CASCADE,
  -- Check to make sure that it is either:
  -- 1. Provider scoped, i.e. provider_uuid is null
  -- 2. Universe scoped, i.e. universe_uuid is null
  -- 3. Global (at a customer level) scoped, i.e. both are null
  CHECK (
    (universe_uuid IS NOT NULL)::INTEGER +
    (provider_uuid IS NOT NULL)::INTEGER <= 1
  )
);

CREATE TABLE IF NOT EXISTS hook (
  uuid                  UUID NOT NULL,
  customer_uuid         UUID NOT NULL,
  name                  VARCHAR(100) NOT NULL,
  execution_lang        VARCHAR(64) NOT NULL,
  hook_text             TEXT NOT NULL,
  use_sudo              BOOLEAN NOT NULL,
  runtime_args          TEXT,
  hook_scope_uuid       UUID,
  CONSTRAINT pk_hook PRIMARY KEY (uuid),
  CONSTRAINT fk_hook_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT fk_hook_scope FOREIGN KEY(hook_scope_uuid) REFERENCES hook_scope(uuid) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT uq_customer_name UNIQUE(customer_uuid, name)
);
