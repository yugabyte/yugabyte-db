-- Copyright (c) YugaByte, Inc.

DROP FUNCTION create_customer_alert_definitions(text,text,boolean);
CREATE FUNCTION create_customer_alert_definitions(configurationName text, queryTemplate text, skipTargetLabels boolean)
 RETURNS boolean
 language plpgsql
 as
$$
  DECLARE
    customerRecord RECORD;
    configurationUuid UUID;
    definitionUuid UUID;
    query TEXT;
  BEGIN
    FOR customerRecord IN
      SELECT * FROM customer
    LOOP
      configurationUuid :=  (select uuid from alert_configuration
        where name = configurationName and customer_uuid = customerRecord.uuid);
      definitionUuid := gen_random_uuid();
      query := replace(queryTemplate, '__customerUuid__', customerRecord.uuid::text);
      insert into alert_definition
        (uuid, query, customer_uuid, configuration_uuid)
      values
        (definitionUuid, query, customerRecord.uuid, configurationUuid);
      insert into alert_definition_label
        (definition_uuid, name, value)
      values
        (definitionUuid, 'source_type', 'customer'),
        (definitionUuid, 'customer_name', customerRecord.name),
        (definitionUuid, 'customer_uuid', customerRecord.uuid),
        (definitionUuid, 'source_name', customerRecord.name),
        (definitionUuid, 'source_uuid', customerRecord.uuid);
    END LOOP;
    RETURN true;
  END;
$$;

 -- Backup Deletion Failure
 insert into alert_configuration
  (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds, threshold_unit, template, active, default_destination)
select
  gen_random_uuid(),
  uuid,
  'Backup Deletion Failed',
  'Failed to delete Backup',
  current_timestamp,
  'PLATFORM',
  '{"all":true}',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":0}}',
  'STATUS',
  'BACKUP_DELETION_FAILURE',
  true,
  true
from customer;

select create_customer_alert_definitions(
 'Backup Deletion Failed',
 'ybp_delete_backup_failure{customer_uuid="__customerUuid__"}'
   || ' {{ query_condition }} {{ query_threshold }}',
   true);
