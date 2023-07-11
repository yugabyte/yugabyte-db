-- Copyright (c) YugaByte, Inc.

DROP FUNCTION IF EXISTS create_universe_alert_definitions(text,text);
CREATE OR REPLACE FUNCTION create_universe_alert_definitions(configurationName text)
 RETURNS boolean
 language plpgsql
 as
$$
  declare
    customerRecord record;
    universeRecord record;
    configurationUuid uuid;
    definitionUuid uuid;
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
        insert into alert_definition
          (uuid, customer_uuid, configuration_uuid)
        values
          (definitionUuid, customerRecord.uuid, configurationUuid);
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

DROP FUNCTION IF EXISTS create_customer_alert_definitions(text,text);
DROP FUNCTION IF EXISTS create_customer_alert_definitions(text,text,boolean);
CREATE OR REPLACE FUNCTION create_customer_alert_definitions(configurationName text, skipTargetLabels boolean)
 RETURNS boolean
 language plpgsql
 as
$$
  DECLARE
    customerRecord RECORD;
    configurationUuid UUID;
    definitionUuid UUID;
  BEGIN
    FOR customerRecord IN
      SELECT * FROM customer
    LOOP
      configurationUuid :=  (select uuid from alert_configuration
        where name = configurationName and customer_uuid = customerRecord.uuid);
      definitionUuid := gen_random_uuid();
      insert into alert_definition
        (uuid, customer_uuid, configuration_uuid)
      values
        (definitionUuid, customerRecord.uuid, configurationUuid);
      IF NOT skipTargetLabels THEN
        insert into alert_definition_label
          (definition_uuid, name, value)
        values
          (definitionUuid, 'source_type', 'customer'),
          (definitionUuid, 'customer_name', customerRecord.name),
          (definitionUuid, 'customer_uuid', customerRecord.uuid),
          (definitionUuid, 'source_name', customerRecord.name),
          (definitionUuid, 'source_uuid', customerRecord.uuid);
      END IF;
    END LOOP;
    RETURN true;
  END;
$$;

DROP FUNCTION IF EXISTS replace_configuration_query;
