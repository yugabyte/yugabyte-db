-- Copyright (c) YugaByte, Inc.

DELETE FROM troubleshooting_platform;
ALTER TABLE troubleshooting_platform ADD COLUMN api_token bytea NOT NULL;
ALTER TABLE troubleshooting_platform ADD COLUMN metrics_scrape_period_secs BIGINT NOT NULL;
