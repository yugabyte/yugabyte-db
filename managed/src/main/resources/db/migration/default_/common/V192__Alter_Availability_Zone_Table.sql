-- Copyright (c) YugaByte, Inc.
ALTER TABLE availability_zone ALTER COLUMN subnet TYPE varchar(80);
ALTER TABLE availability_zone ALTER COLUMN secondary_subnet TYPE varchar(80);
