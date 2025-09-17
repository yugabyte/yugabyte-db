-- Copyright (c) YugabyteDB, Inc.
ALTER TABLE availability_zone ALTER COLUMN subnet TYPE varchar(500);
ALTER TABLE availability_zone ALTER COLUMN secondary_subnet TYPE varchar(500);