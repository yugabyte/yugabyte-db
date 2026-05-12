-- Update pg_stat_statements to version 1.10-yb-2.1
DO $$
BEGIN
    IF NOT EXISTS (
      -- Checks for column docdb_nexts
        SELECT TRUE FROM pg_attribute
        WHERE attrelid = 'pg_catalog.pg_stat_statements'::regclass
            AND attname = 'docdb_nexts'
            AND NOT attisdropped
    ) THEN

    ALTER EXTENSION pg_stat_statements UPDATE TO '1.10-yb-2.1';

    END IF;
END $$;
