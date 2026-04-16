-- Copyright (c) YugaByte, Inc.
alter table alert_definition add column if not exists query_threshold double precision;

-- In case value is stored in universe runtime config
update alert_definition ad
set query_threshold =
 (select cast(c.value as double precision)
  from alert_definition_label l
  left join runtime_config_entry c on l.value = c.scope_uuid::text
  where l.definition_uuid = ad.uuid and l.name = 'universe_uuid' and c.path = 'yb.alert.max_clock_skew_ms')
where name = 'Clock Skew Alert' and query_threshold is null;

-- In case value is stored in global runtime config
update alert_definition ad
set query_threshold =
 (select cast(c.value as double precision)
  from runtime_config_entry c
  where c.path = 'yb.alert.max_clock_skew_ms')
where name = 'Clock Skew Alert' and query_threshold is null;

-- In case default value is used
update alert_definition ad
set query_threshold = 500
where name = 'Clock Skew Alert' and query_threshold is null;

-- In case value is stored in universe runtime config
update alert_definition ad
set query_threshold =
 (select cast(c.value as double precision)
  from alert_definition_label l
  left join runtime_config_entry c on l.value = c.scope_uuid::text
  where l.definition_uuid = ad.uuid and l.name = 'universe_uuid' and c.path = 'yb.alert.max_memory_cons_pct')
where name = 'Memory Consumption Alert' and query_threshold is null;

-- In case value is stored in global runtime config
update alert_definition ad
set query_threshold =
 (select cast(c.value as double precision)
  from runtime_config_entry c
  where c.path = 'yb.alert.max_memory_cons_pct')
where name = 'Memory Consumption Alert' and query_threshold is null;

-- In case default value is used
update alert_definition ad
set query_threshold = 90
where name = 'Memory Consumption Alert' and query_threshold is null;

-- In case value is stored in universe runtime config
update alert_definition ad
set query_threshold =
 (select cast(c.value as double precision)
  from alert_definition_label l
  left join runtime_config_entry c on l.value = c.scope_uuid::text
  where l.definition_uuid = ad.uuid and l.name = 'universe_uuid' and c.path = 'yb.alert.replication_lag_ms')
where name = 'Replication Lag Alert' and query_threshold is null;

-- In case value is stored in global runtime config
update alert_definition ad
set query_threshold =
 (select cast(c.value as double precision)
  from runtime_config_entry c
  where c.path = 'yb.alert.replication_lag_ms')
where name = 'Replication Lag Alert' and query_threshold is null;

-- This hack is required because alert threshold is stored in the query itself for this alert sometimes.
update alert_definition ad
set query_threshold = cast(nullif(regexp_replace(regexp_replace(query, '.*\s>\s', ''), '[^0-9E\.]+', '', 'g'), '') as double precision),
query = regexp_replace(query, '\s>\s.*', '\s>\s{{ yb.alert.replication_lag_ms }}')
where name = 'Replication Lag Alert' and query_threshold is null;

-- In case default value is used
update alert_definition ad
set query_threshold = 180000
where name = 'Replication Lag Alert' and query_threshold is null;

update alert_definition
set query = regexp_replace(query, '{{.*}}', '{{ query_threshold }}');

alter table alert_definition alter column query_threshold set not null;
