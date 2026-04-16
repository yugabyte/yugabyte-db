-- Copyright (c) YugaByte, Inc.

create or replace function create_universe_alert_definitions(configurationName text, queryTemplate text)
 returns boolean
 language plpgsql
 as
$$
  declare
    customerRecord record;
    universeRecord record;
    configurationUuid uuid;
    definitionUuid uuid;
    query text;
  begin
    for customerRecord in
      select * from customer
    loop
      configurationUuid :=  (select uuid from alert_configuration
        where name = configurationName and customer_uuid = customerRecord.uuid);
      for universeRecord in
        select * from universe where universe.customer_id = customerRecord.id
      loop
        definitionUuid := gen_random_uuid();
        query := replace(queryTemplate, '__universeUuid__', universeRecord.universe_uuid::text);
        query := replace(query, '__nodePrefix__', universeRecord.universe_details_json::jsonb->>'nodePrefix');
        insert into alert_definition
          (uuid, query, customer_uuid, configuration_uuid)
        values
          (definitionUuid, query, customerRecord.uuid, configurationUuid);
        insert into alert_definition_label
          (definition_uuid, name, value)
        values
          (definitionUuid, 'source_type', 'universe'),
          (definitionUuid, 'source_name', universeRecord.name),
          (definitionUuid, 'source_uuid', universeRecord.universe_uuid),
          (definitionUuid, 'universe_name', universeRecord.name),
          (definitionUuid, 'universe_uuid', universeRecord.universe_uuid),
          (definitionUuid, 'node_prefix', universeRecord.universe_details_json::jsonb->>'nodePrefix');
      end loop;
    end loop;
    return TRUE;
  end;
$$;

-- DB_DRIVE_FAILURE
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB drive failure',
  'TServer detected drive failure',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_DRIVE_FAILURE',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'DB drive failure',
 'count by (node_prefix) (drive_fault{node_prefix="__nodePrefix__", export_type="tserver_export"})'
   || ' {{ query_condition }} {{ query_threshold }}');

