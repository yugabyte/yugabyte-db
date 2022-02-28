-- Copyright (c) YugaByte, Inc.

-- Deleting backup having deleted state.
DELETE FROM backup where state='Deleted';
