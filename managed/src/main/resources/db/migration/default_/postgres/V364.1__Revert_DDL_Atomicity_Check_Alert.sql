-- Copyright (c) YugaByte, Inc.

 -- Revert DDL Atomicity check alert
delete from alert_configuration where template = 'DDL_ATOMICITY_CHECK';
delete from schema_version where version = '364';
