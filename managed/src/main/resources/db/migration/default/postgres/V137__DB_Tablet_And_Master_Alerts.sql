-- Copyright (c) YugaByte, Inc.

-- MASTER_LEADER_MISSING
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, duration_sec, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Master Leader missing',
  'Master Leader is missing for configured duration',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"LESS_THAN", "threshold":1}}',
  'STATUS',
  'MASTER_LEADER_MISSING',
  300,
  true,
  true
from customer;

select create_universe_alert_definitions(
 'Master Leader missing',
 'max by (node_prefix) (yb_node_is_master_leader{node_prefix="__nodePrefix__"})'
   || ' {{ query_condition }} {{ query_threshold }}');

-- LEADERLESS_TABLETS
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Leaderless tablets',
  'Leader is missing for some tablet(s) for more than 5 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'STATUS',
  'LEADERLESS_TABLETS',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'Leaderless tablets',
 'count by (node_prefix) (max_over_time(yb_node_leaderless_tablet{node_prefix="__nodePrefix__"}[5m])'
   || ' {{ query_condition }} {{ query_threshold }})');

-- UNDER_REPLICATED_TABLETS
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Under-replicated tablets',
  'Some tablet(s) remain under-replicated for more than 5 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'STATUS',
  'UNDER_REPLICATED_TABLETS',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'Under-replicated tablets',
 'count by (node_prefix) (max_over_time(yb_node_underreplicated_tablet{node_prefix="__nodePrefix__"}[5m])'
   || ' {{ query_condition }} {{ query_threshold }})');
