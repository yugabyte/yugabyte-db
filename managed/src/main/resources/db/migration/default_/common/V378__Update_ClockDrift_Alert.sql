update alert_definition set config_written = false where configuration_uuid IN
  (select uuid from alert_configuration where template = 'NODE_CLOCK_DRIFT')