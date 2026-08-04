-- Note, if you installed update 2.6.3 prior to the release of 2.6.4, this update will not change anything, but is still required to be installed to allow the extension version update chain to work.
-- While upgrading to version 2.6.3 or later, if you encounter the error: "cannot ALTER TABLE "part_config_sub" because it has pending trigger events" (or for the part_config table), then you must install that update by itself first and then install the update to 2.6.4 separately in a different transaction. Changing the values of a column in the same transaction as altering it to add/remove a constraint can cause the pending triggers error. The only way around that is to do the steps as separate transactions, which this update separates out from the previous one that caused the issue. Unfortunately, all extension updates run in a single transaction unless you upgrade manually to a specific version to control the transaction state between versions. (Github Issue #167).

/*
If you encounter this error, do the updates for 2.6.3 & 2.6.4 as follows:

    BEGIN;
    ALTER EXTENSION pg_partman UPDATE TO '2.6.3';
    COMMIT;

    BEGIN;
    ALTER EXTENSION pg_partman UPDATE TO '2.6.4';
    COMMIT;
*/

-- Redoing this again shouldn't matter
ALTER TABLE @extschema@.part_config ALTER COLUMN epoch SET DEFAULT 'none';

ALTER TABLE @extschema@.part_config_sub ALTER COLUMN sub_epoch SET DEFAULT 'none';

-- Only recreate constraints if they don't already exist from a previous 2.6.3 update before 2.6.4 was available
DO $$
DECLARE
v_exists    text;
BEGIN
    SELECT conname INTO v_exists 
    FROM pg_catalog.pg_constraint t
    JOIN pg_catalog.pg_class c ON t.conrelid = c.oid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE t.conname = 'part_config_epoch_check'
    AND c.relname = 'part_config'
    AND n.nspname = '@extschema@';

    IF v_exists IS NULL THEN
        EXECUTE format('
            ALTER TABLE @extschema@.part_config
            ADD CONSTRAINT part_config_epoch_check 
            CHECK (@extschema@.check_epoch_type(epoch))');
    END IF;
    v_exists := NULL;

    SELECT conname INTO v_exists 
    FROM pg_catalog.pg_constraint t
    JOIN pg_catalog.pg_class c ON t.conrelid = c.oid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE t.conname = 'part_config_sub_epoch_check'
    AND c.relname = 'part_config_sub'
    AND n.nspname = '@extschema@';

    IF v_exists IS NULL THEN
        EXECUTE format('
            ALTER TABLE @extschema@.part_config_sub
            ADD CONSTRAINT part_config_sub_epoch_check 
            CHECK (@extschema@.check_epoch_type(sub_epoch))');
    END IF;
END
$$;


