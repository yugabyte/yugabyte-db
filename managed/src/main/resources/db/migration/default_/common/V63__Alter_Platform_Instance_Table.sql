-- Copyright (c) YugabyteDB, Inc.

alter table if exists high_availability_config add column last_failover timestamp;
