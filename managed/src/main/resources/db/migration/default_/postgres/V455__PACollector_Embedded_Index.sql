-- Copyright (c) YugabyteDB, Inc.

-- Enforce at most one embedded row per customer (partial unique index).
-- Kept postgres-only because H2 does not support the WHERE clause on CREATE
-- UNIQUE INDEX; H2 tests rely on the app-level guarantee in
-- EmbeddedCollectorInitializer instead.
CREATE UNIQUE INDEX ix_pa_collector_embedded_per_customer
  ON pa_collector(customer_uuid) WHERE embedded = TRUE;
