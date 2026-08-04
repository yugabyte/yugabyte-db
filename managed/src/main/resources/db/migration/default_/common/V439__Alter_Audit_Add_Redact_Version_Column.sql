-- Copyright (c) YugabyteDB, Inc.

ALTER TABLE audit ADD COLUMN last_redacted_version INTEGER NOT NULL DEFAULT 0;
CREATE INDEX ix_audit_last_redacted_version ON audit (last_redacted_version);
