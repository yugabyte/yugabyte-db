-- Copyright (c) YugabyteDB, Inc.

ALTER TABLE pa_collector ADD COLUMN embedded BOOLEAN NOT NULL DEFAULT FALSE;
-- Backfill: rows created by EmbeddedCollectorInitializer use uuid = customer_uuid.
UPDATE pa_collector SET embedded = TRUE WHERE uuid = customer_uuid;
-- The "at most one embedded row per customer" partial unique index lives in the
-- postgres/ side of the migration set (V455). H2 does not support partial indexes
-- and this is a safety net on top of the app-level guarantee in
-- EmbeddedCollectorInitializer, which is single-threaded per customer.
