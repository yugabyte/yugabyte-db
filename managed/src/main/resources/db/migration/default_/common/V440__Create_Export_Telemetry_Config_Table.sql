-- Copyright (c) YugabyteDB, Inc.
-- Unified telemetry export config: one row per universe (source of truth for export config)

CREATE TABLE IF NOT EXISTS export_telemetry_config (
  universe_uuid      UUID NOT NULL,
  telemetry_config   JSON_ALIAS,
  created_at         TIMESTAMP DEFAULT (CURRENT_TIMESTAMP AT TIME ZONE 'utc') NOT NULL,
  updated_at         TIMESTAMP DEFAULT (CURRENT_TIMESTAMP AT TIME ZONE 'utc') NOT NULL,
  CONSTRAINT pk_export_telemetry_config PRIMARY KEY (universe_uuid),
  CONSTRAINT fk_export_telemetry_config_universe_uuid FOREIGN KEY (universe_uuid) REFERENCES universe(universe_uuid) ON DELETE CASCADE
);
