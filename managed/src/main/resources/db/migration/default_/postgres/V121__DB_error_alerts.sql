-- Copyright (c) YugaByte, Inc.

DROP FUNCTION create_universe_alert_definitions(text,text);
CREATE FUNCTION create_universe_alert_definitions(configurationName text, queryTemplate text)
 RETURNS boolean
 language plpgsql
 as
$$
  DECLARE
    customerRecord RECORD;
    universeRecord RECORD;
    configurationUuid UUID;
    definitionUuid UUID;
    query TEXT;
  BEGIN
    FOR customerRecord IN
      SELECT * FROM customer
    LOOP
      configurationUuid :=  (select uuid from alert_configuration
        where name = configurationName and customer_uuid = customerRecord.uuid);
      FOR universeRecord IN
        SELECT * FROM universe where universe.customer_id = customerRecord.id
      LOOP
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
          (definitionUuid, 'universe_uuid', universeRecord.universe_uuid);
      END LOOP;
    END LOOP;
    RETURN true;
  END;
$$;

-- DB_ERROR_LOGS
insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'DB error logs',
  'Error logs detected on DB Master/TServer instances',
  current_timestamp,
  'UNIVERSE',
  '{"all":true}',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":0}}',
  'COUNT',
  'DB_ERROR_LOGS',
  true,
  true
from customer;

select create_universe_alert_definitions(
 'DB error logs',
 'sum by (universe_uuid) (ybp_health_check_node_master_error_logs{universe_uuid="__universeUuid__"} < bool 1 '
    ||'* ybp_health_check_node_master_fatal_logs{universe_uuid="__universeUuid__"} == bool 1) '
    || '+ sum by (universe_uuid) (ybp_health_check_node_tserver_error_logs{universe_uuid="__universeUuid__"} < bool 1 '
    ||'* ybp_health_check_node_tserver_fatal_logs{universe_uuid="__universeUuid__"} == bool 1) '
    || '{{ query_condition }} {{ query_threshold }}');

-- DB_FATAL_LOGS
select replace_configuration_query(
 'DB_FATAL_LOGS',
 'sum by (universe_uuid) (ybp_health_check_node_master_fatal_logs{universe_uuid="__universeUuid__"} < bool 1) '
     || '+ sum by (universe_uuid) (ybp_health_check_node_tserver_fatal_logs{universe_uuid="__universeUuid__"} < bool 1) '
     || '{{ query_condition }} {{ query_threshold }}');
