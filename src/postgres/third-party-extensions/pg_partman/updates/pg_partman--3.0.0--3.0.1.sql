-- IMPORTANT NOTE: The initial version of pg_partman 3 had to be split into two updates due to changing both the data in the config table as well as adding constraints on that data. Depending on the data contained in the config table, doing this in a single-transaction update may not work (you may see an error about pending trigger events). If this is the case, please update to version 3.0.1 in a separate transaction from your update to 3.0.0. Example:
    
    /*
        BEGIN;
        ALTER EXTENSION pg_partman UPDATE TO '3.0.0';
        COMMIT;

        BEGIN;
        ALTER EXTENSION pg_partman UPDATE TO '3.0.1';
        COMMIT;
    */

-- Update 3.0.1 MUST be installed for version 3.x of pg_partman to work properly. As long as these updates are run within a few seconds of each other, there should be no issues.

ALTER TABLE @extschema@.part_config DROP CONSTRAINT part_config_type_check;
ALTER TABLE @extschema@.part_config_sub DROP CONSTRAINT part_config_sub_type_check;

UPDATE @extschema@.part_config SET partition_type = 'partman' WHERE partition_type <> 'time-custom';
UPDATE @extschema@.part_config_sub SET sub_partition_type = 'partman' WHERE sub_partition_type <> 'time-custom';

ALTER TABLE @extschema@.part_config
ADD CONSTRAINT part_config_type_check 
CHECK (@extschema@.check_partition_type(partition_type));

ALTER TABLE @extschema@.part_config_sub
ADD CONSTRAINT part_config_sub_type_check
CHECK (@extschema@.check_partition_type(sub_partition_type));

ALTER TABLE @extschema@.part_config
ADD CONSTRAINT part_config_automatic_maintenance_check
CHECK (@extschema@.check_automatic_maintenance_value(automatic_maintenance));

ALTER TABLE @extschema@.part_config_sub
ADD CONSTRAINT part_config_sub_automatic_maintenance_check
CHECK (@extschema@.check_automatic_maintenance_value(sub_automatic_maintenance));
