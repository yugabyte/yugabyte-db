-- Move an installed yb_ycql_utils to 1.2 (yb_latency_histogram).
-- Skip databases that never created the extension. Skip when the column is
-- already present so a repeated run does not ALTER EXTENSION to the current version.
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'yb_ycql_utils')
       AND NOT EXISTS (
            SELECT 1
            FROM pg_extension e
            JOIN pg_depend d
              ON d.refobjid = e.oid
             AND d.deptype = 'e'
             AND d.classid = 'pg_class'::regclass
            JOIN pg_class c ON c.oid = d.objid
            JOIN pg_attribute a ON a.attrelid = c.oid
            WHERE e.extname = 'yb_ycql_utils'
              AND c.relname = 'ycql_stat_statements'
              AND a.attname = 'yb_latency_histogram'
              AND NOT a.attisdropped
       ) THEN
        ALTER EXTENSION yb_ycql_utils UPDATE TO '1.2';
    END IF;
END $$;
