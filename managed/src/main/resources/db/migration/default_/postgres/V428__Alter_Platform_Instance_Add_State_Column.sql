-- Copyright (c) YugabyteDB, Inc.

CREATE UNIQUE INDEX ix_single_leader ON platform_instance (state) WHERE state = 'LEADER';
CREATE UNIQUE INDEX ix_single_is_local ON platform_instance (is_local) WHERE is_local = true;