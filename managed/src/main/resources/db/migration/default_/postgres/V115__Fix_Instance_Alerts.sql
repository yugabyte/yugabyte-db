-- Copyright (c) YugaByte, Inc.

CREATE OR REPLACE FUNCTION replace_configuration_query(alertTemplate text, queryTemplate text)
 RETURNS boolean
 language plpgsql
 as
$$
  DECLARE
    customerRecord RECORD;
    universeRecord RECORD;
    definitionRecord RECORD;
    newQuery TEXT;
  BEGIN
    FOR customerRecord IN
      SELECT * FROM customer
    LOOP
      FOR universeRecord IN
        SELECT * FROM universe where universe.customer_id = customerRecord.id
      LOOP
        FOR definitionRecord IN
          -- Select universe alert definitions for particular universe and template
          SELECT * FROM alert_definition where customer_uuid = customerRecord.uuid and
           configuration_uuid in (select uuid from alert_configuration
             where template = alertTemplate and target_type = 'UNIVERSE') and
           uuid in (select distinct definition_uuid from alert_definition_label
            where name = 'universe_uuid' and value = universeRecord.universe_uuid::text)
        LOOP
          newQuery := replace(queryTemplate, '__universeUuid__', universeRecord.universe_uuid::text);
          newQuery := replace(newQuery, '__nodePrefix__', universeRecord.universe_details_json::jsonb->>'nodePrefix');
          update alert_definition set query = newQuery, config_written = false where uuid = definitionRecord.uuid;
        END LOOP;
      END LOOP;
      FOR definitionRecord IN
          -- Select platform alert definitions for particular customer
        SELECT * FROM alert_definition where customer_uuid = customerRecord.uuid and
           configuration_uuid in (select uuid from alert_configuration
             where template = alertTemplate and target_type = 'PLATFORM')
      LOOP
        newQuery := replace(queryTemplate, '__customerUuid__', customerRecord.uuid::text);
        update alert_definition set query = newQuery, config_written = false where uuid = definitionRecord.uuid;
      END LOOP;
    END LOOP;
    RETURN true;
  END;
$$;

select replace_configuration_query(
 'DB_INSTANCE_DOWN',
 'count by (node_prefix) (label_replace(max_over_time('
   || 'up{export_type=~"master_export|tserver_export",node_prefix="__nodePrefix__"}[15m]), '
   || '"exported_instance", "$1", "instance", "(.*)") < 1 and on (node_prefix, export_type, exported_instance) '
   || '(min_over_time(ybp_universe_node_function{node_prefix="__nodePrefix__"}[15m]) == 1)) '
   || '{{ query_condition }} {{ query_threshold }}');

select replace_configuration_query(
 'DB_INSTANCE_RESTART',
 'max by (universe_uuid) (label_replace(changes('
   || 'ybp_health_check_master_boot_time_sec{universe_uuid="__universeUuid__"}[30m]) '
   || 'and on (universe_uuid) (max_over_time(ybp_universe_update_in_progress{universe_uuid="__universeUuid__"}[30m]) == 0), '
   || '"export_type", "master_export", "universe_uuid",".*") or '
   || '(label_replace(changes(ybp_health_check_tserver_boot_time_sec{universe_uuid="__universeUuid__"}[30m]) '
   || 'and on (universe_uuid) (max_over_time(ybp_universe_update_in_progress{universe_uuid="__universeUuid__"}[30m]) == 0), '
   || '"export_type", "tserver_export", "universe_uuid",".*"))) '
   || '{{ query_condition }} {{ query_threshold }}');
