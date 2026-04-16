-- Copyright (c) YugaByte, Inc.

-- Update the varchar length to accomodate FQDN.
ALTER TABLE node_agent ALTER COLUMN ip TYPE varchar(255);
