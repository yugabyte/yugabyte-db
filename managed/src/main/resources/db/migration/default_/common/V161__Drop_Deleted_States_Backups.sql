-- Copyright (c) YugabyteDB, Inc.

-- Deleting backup having deleted state.
DELETE FROM backup where state='Deleted';
