-- Copyright (c) YugaByte, Inc.

-- Change last_failover column from timestamp without tz to epoch time in ms
ALTER TABLE high_availability_config
ALTER COLUMN last_failover TYPE bigint
USING (EXTRACT(EPOCH FROM last_failover) * 1000)::bigint;