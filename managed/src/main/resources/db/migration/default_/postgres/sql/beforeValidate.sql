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

       -- Rerun migration 296
       DELETE FROM SCHEMA_VERSION where version = '296' AND checksum = -1228390187;

       -- Fix the migration checksum
       UPDATE schema_version SET checksum = -493663200 WHERE version = '290' AND checksum = 1818940336;
       
       -- Rerun migration 305 after removing the existing fk constraints
       IF EXISTS (SELECT * FROM schema_version WHERE version = '305' AND checksum = -1750313508) THEN
        DELETE FROM SCHEMA_VERSION where version = '305' AND checksum = -1750313508;
        ALTER TABLE customer_task DROP CONSTRAINT uk_task_uuid;
        ALTER TABLE customer_task DROP CONSTRAINT fk_customer_task_task_info;
       END IF;

       -- Fix the migration checksum
       UPDATE schema_version SET checksum = 828796405 WHERE version = '342' AND checksum = 1950473975;

      -- Fix the migration checksum
       UPDATE schema_version SET checksum = -1546684527 WHERE version = '223' AND checksum = 105101608;

       -- PLAT-23479: master and the 2025.2 / 2026.1 release branches numbered the same two
       -- migrations the other way round. The release branches shipped
       -- V466 = XCluster Table Bad State Alert Duration, while master had
       -- V466 = Recreate swamper targets and V467 = XCluster Table Bad State Alert Duration.
       -- Master now uses the released numbering, so a database migrated under the old master
       -- numbering only needs its history rows relabelled: both migrations already ran. Checksums
       -- are matched exactly, so any other mismatch on these versions still fails validation.
       IF EXISTS (SELECT 1 FROM schema_version
                  WHERE version = '466' AND checksum = 1316434762) THEN
         IF EXISTS (SELECT 1 FROM schema_version
                    WHERE version = '467' AND checksum = 1058880285) THEN
           UPDATE schema_version
           SET description = 'XCluster Table Bad State Alert Duration',
               script = 'V466__XCluster_Table_Bad_State_Alert_Duration.sql',
               checksum = 1058880285
           WHERE version = '466' AND checksum = 1316434762;
           UPDATE schema_version
           SET description = 'Recreate swamper targets',
               script = 'V467__Recreate_swamper_targets.sql',
               checksum = 1316434762
           WHERE version = '467' AND checksum = 1058880285;
         ELSIF NOT EXISTS (SELECT 1 FROM schema_version WHERE version = '467') THEN
           -- Only the swamper migration ran under the old numbering. Move it to V467 so that
           -- the xCluster migration is still applied as V466.
           UPDATE schema_version
           SET version = '467', script = 'V467__Recreate_swamper_targets.sql'
           WHERE version = '466' AND checksum = 1316434762;
         END IF;
       END IF;

       -- Fix next migration here
    END IF;
  END;
$$;
