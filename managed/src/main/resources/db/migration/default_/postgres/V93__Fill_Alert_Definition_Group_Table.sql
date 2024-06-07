-- Copyright (c) YugaByte, Inc.

DO $$
declare
  value_type text;
begin
  select data_type into value_type from information_schema.columns
    where table_schema = 'public' and table_name= 'runtime_config_entry' and column_name = 'value';
  if value_type = 'text' then
    alter table runtime_config_entry alter column value type bytea using convert_to(value::text, 'utf8');
  end if;
end;
$$ language plpgsql;

CREATE OR REPLACE FUNCTION get_default_threshold(customer_uuid uuid, name text, default_value double precision)
 RETURNS double precision
 language plpgsql
 as
$$
  DECLARE
    customer_value double precision;
    global_value double precision;
  BEGIN
    customer_value := (select cast(convert_from(c.value, 'UTF-8') as double precision)
                        from runtime_config_entry c
                        where c.scope_uuid = customer_uuid and c.path = name);
    global_value := (select cast(convert_from(c.value, 'UTF-8') as double precision)
                      from runtime_config_entry c
                      where c.scope_uuid = '00000000-0000-0000-0000-000000000000' and c.path = name);
    RETURN COALESCE(customer_value, global_value, default_value);
  END;
$$;

CREATE OR REPLACE FUNCTION gen_random_uuid()
 RETURNS uuid
 language plpgsql
 as
$$
  BEGIN
    RETURN md5(random()::text || clock_timestamp()::text)::uuid;
  END;
$$;

insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, template, active)
select
  gen_random_uuid(),
  uuid,
  'Memory Consumption',
  'Average node memory consumption percentage for 10 minutes is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":' || get_default_threshold(uuid, 'yb.alert.max_memory_cons_pct', 90) || '}}',
  'MEMORY_CONSUMPTION',
  true
from customer;

insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, template, active)
select
  gen_random_uuid(),
  uuid,
  'Clock Skew',
  'Max universe clock skew in ms is above threshold during last 10 minutes',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":' || get_default_threshold(uuid, 'yb.alert.max_clock_skew_ms', 500) || '}}',
  'CLOCK_SKEW',
  true
from customer;

update alert_definition ad
set
  group_uuid = (
    select uuid
    from alert_definition_group adg
    where adg.customer_uuid = ad.customer_uuid
      and adg.name = replace(ad.name, ' Alert', ''))
where ad.name in ('Memory Consumption Alert', 'Clock Skew Alert');

insert into alert_definition_group
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, template, active)
select
  gen_random_uuid(),
  ad.customer_uuid,
  'Replication Lag',
  'Average universe replication lag for 10 minutes in ms is above threshold',
  current_timestamp,
  'UNIVERSE',
  '{"all":false,"uuids":["' || adl.value || '"]}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":' || ad.query_threshold || '}}',
  'REPLICATION_LAG',
  ad.active
from alert_definition ad
left join alert_definition_label adl on ad.uuid = adl.definition_uuid where ad.name = 'Replication Lag Alert' and adl.name = 'universe_uuid';

update alert_definition ad
set
  group_uuid = (
    select uuid
    from alert_definition_group adg
    where adg.customer_uuid = ad.customer_uuid
      and adg.name = replace(ad.name, ' Alert', '')
      and replace((adg.target::json #>'{uuids,0}')::text, '"', '') = (select value from alert_definition_label adl where ad.uuid = adl.definition_uuid and adl.name = 'universe_uuid'))
where ad.name in ('Replication Lag Alert');

alter table alert_definition drop column if exists name;
alter table alert_definition drop column if exists query_duration_sec;
alter table alert_definition drop column if exists query_threshold;
alter table alert_definition drop column if exists active;
alter table alert_definition alter column group_uuid set not null;
update alert_definition set config_written = false;

drop function if exists get_default_threshold(customer_uuid uuid, name text, default_value double precision);
