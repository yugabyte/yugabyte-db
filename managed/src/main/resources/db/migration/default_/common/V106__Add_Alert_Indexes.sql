-- Copyright (c) YugaByte, Inc.

-- To make sure we GC all the old resolved alerts at some point
update alert set resolved_time = create_time where state = 'RESOLVED' and resolved_time is null;

create index ix_alert_customer_resolved_time on alert (customer_uuid, resolved_time);
create index ix_alert_customer_state on alert (customer_uuid, state);
create index ix_alert_customer_severity on alert (customer_uuid, severity);
create index ix_alert_customer_target_name on alert (customer_uuid, target_name);
create index ix_alert_customer_group_type on alert (customer_uuid, group_type);
create index ix_alert_group_uuid on alert (group_uuid);
create index ix_alert_definition_uuid on alert (definition_uuid);
