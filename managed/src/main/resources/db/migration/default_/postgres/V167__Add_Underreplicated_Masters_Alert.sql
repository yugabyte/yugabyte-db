-- Copyright (c) YugaByte, Inc.

-- MASTER_UNDER_REPLICATED
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Under-replicated master',
  'Master is missing from raft group or has follower lag higher than threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":900}}',
  'SECOND',
  'MASTER_UNDER_REPLICATED',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'Under-replicated master',
 '(min_over_time((ybp_universe_replication_factor{node_prefix="__nodePrefix__"} - on(node_prefix)'
    || ' count by(node_prefix) (count by (node_prefix, exported_instance)(follower_lag_ms'
    || '{export_type="master_export", node_prefix="__nodePrefix__"})))[{{ query_threshold }}s:]) > 0'
    || ' or (max by(node_prefix) (follower_lag_ms{export_type="master_export",'
    || ' node_prefix="__nodePrefix__"}) {{ query_condition }} ({{ query_threshold }} * 1000)))');

-- As we now evaluate alerting rules each 2 minutes - we want alert to be raised on first fire.
-- Otherwise it will wait for second evaluation - which is 2 more minutes.
-- Exception is alert configs with duration set to > 15 seconds.

alter table alert_configuration alter column duration_sec set default 0;
update alert_configuration set duration_sec = 0 where duration_sec <= 15;
update alert_definition set config_written = false;
