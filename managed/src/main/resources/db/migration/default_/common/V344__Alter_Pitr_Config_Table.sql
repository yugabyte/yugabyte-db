-- Copyright (c) YugaByte, Inc.

ALTER TABLE pitr_config ADD COLUMN created_for_dr boolean DEFAULT false NOT NULL;

UPDATE pitr_config SET created_for_dr = true where uuid in
    (SELECT  pitr_uuid FROM xcluster_pitr);

