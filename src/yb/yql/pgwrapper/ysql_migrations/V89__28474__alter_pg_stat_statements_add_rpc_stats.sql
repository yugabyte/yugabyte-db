-- Update pg_stat_statements to version 1.10-yb-2.0
DO $$
BEGIN
    IF NOT EXISTS (
      -- Checks for column docdb_read_rpcs
        SELECT TRUE FROM pg_attribute
        WHERE attrelid = 'pg_catalog.pg_stat_statements'::regclass
            AND attname = 'docdb_read_rpcs'
            AND NOT attisdropped
    ) THEN

    ALTER EXTENSION pg_stat_statements UPDATE TO '1.10-yb-2.0';

    END IF;
END $$;
