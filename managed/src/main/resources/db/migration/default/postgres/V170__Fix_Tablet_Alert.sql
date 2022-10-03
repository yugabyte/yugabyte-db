-- Copyright (c) YugaByte, Inc.

update alert_configuration
set thresholds = '{"SEVERE":{"condition":"GREATER_THAN","threshold":300.0}}',
   threshold_unit = 'SECOND'
where template in ('LEADERLESS_TABLETS', 'UNDER_REPLICATED_TABLETS');

-- LEADERLESS_TABLETS
select replace_configuration_query(
 'LEADERLESS_TABLETS',
 'max by (node_prefix) (min_over_time(yb_node_leaderless_tablet_count'
   || '{node_prefix="__nodePrefix__"}[300s]) > 0)');

-- UNDER_REPLICATED_TABLETS
select replace_configuration_query(
 'UNDER_REPLICATED_TABLETS',
 'max by (node_prefix) (min_over_time(yb_node_underreplicated_tablet_count'
   || '{node_prefix="__nodePrefix__"}[300s]) > 0)');
