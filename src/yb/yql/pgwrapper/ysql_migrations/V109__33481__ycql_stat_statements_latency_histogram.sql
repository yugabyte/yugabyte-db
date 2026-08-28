-- Update yb_ycql_utils to version 1.2 (adds the yb_latency_histogram column to
-- ycql_stat_statements). The extension is not installed by default, so only upgrade it when it is
-- already present and has not yet been updated.
DO $$
BEGIN
  IF EXISTS (
    SELECT TRUE FROM pg_extension WHERE extname = 'yb_ycql_utils'
  ) AND NOT EXISTS (
    SELECT TRUE FROM pg_attribute
    WHERE attrelid = to_regclass('ycql_stat_statements')
      AND attname = 'yb_latency_histogram'
      AND NOT attisdropped
  ) THEN

    ALTER EXTENSION yb_ycql_utils UPDATE TO '1.2';

  END IF;
END $$;
