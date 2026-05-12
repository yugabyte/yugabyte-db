-- Copyright (c) YugabyteDB, Inc.

ALTER TABLE troubleshooting_platform RENAME COLUMN tp_url TO pa_url;
ALTER TABLE troubleshooting_platform RENAME COLUMN tp_api_token TO pa_api_token;
ALTER TABLE troubleshooting_platform RENAME TO pa_collector;
ALTER TABLE pa_collector ADD COLUMN metrics_username bytea;
ALTER TABLE pa_collector ADD COLUMN metrics_password bytea;

DELETE FROM audit WHERE action IN (
  'Create Troubleshooting Platform Config',
  'Edit Troubleshooting Platform Config',
  'Delete Troubleshooting Platform Config',
  'Register Universe with Troubleshooting Platform',
  'Unregister Universe from Troubleshooting Platform'
);

UPDATE runtime_config_entry
  SET path = 'yb.ui.feature_flags.enable_pa_collector'
  WHERE path = 'yb.ui.feature_flags.enable_troubleshooting';
