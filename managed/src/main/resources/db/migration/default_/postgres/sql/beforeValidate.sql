DO
$$
  BEGIN
   IF EXISTS (SELECT * FROM information_schema.tables WHERE table_name = 'schema_version') THEN
       -- Fix migration 68
       UPDATE schema_version
       SET type = 'JDBC', checksum = NULL, description = 'Create New Alert Definitions Extra Migration', script = 'db.migration.default.common.V68__Create_New_Alert_Definitions_Extra_Migration'
       WHERE version = '68' AND checksum = -1455400612;

       -- Fix migration 115. Just fix checksum + modify function to have correct declaration.
       IF EXISTS (SELECT * FROM schema_version WHERE version = '115' AND checksum = -1821195795 ) THEN
           UPDATE schema_version SET checksum = 1624093741 WHERE version = '115';
           CREATE OR REPLACE FUNCTION replace_configuration_query(alertTemplate text, queryTemplate text)
            RETURNS boolean
            language plpgsql
            as
           $func1$
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
           $func1$;
       END IF;

       -- Delete migration Alter Architecture Type that was moved from 212 to 216
       DELETE FROM SCHEMA_VERSION WHERE VERSION='212' AND DESCRIPTION='Alter Architecture Type';

       -- Fix migration 93
       UPDATE schema_version SET checksum = -906427156
       WHERE version = '93' AND checksum = 664171038;

       -- Rerun migration 281 with right values
       DELETE FROM SCHEMA_VERSION WHERE VERSION = '281' AND checksum = -875441828;

       -- Fix next migration here
    END IF;
  END;
$$;
