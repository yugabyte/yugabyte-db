-- Update pg_stat_statements to version 1.6-yb-1.0
DO $$
BEGIN
    IF NOT EXISTS (
      -- Checks for column yb_latency_histogram
        SELECT TRUE FROM pg_attribute
        WHERE attrelid = 'pg_catalog.pg_stat_statements'::regclass
            AND attname = 'yb_latency_histogram'
            AND NOT attisdropped
    ) THEN

    ALTER EXTENSION pg_stat_statements UPDATE TO '1.6-yb-1.0';

    END IF;
END $$;
