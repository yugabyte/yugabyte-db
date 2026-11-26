-- Tests for DDLs that run other statements nested beneath them.
CALL TEST_reset();
SELECT yb_xcluster_ddl_replication.get_replication_role();

-- REFRESH MATERIALIZED VIEW CONCURRENTLY internally runs utility statements
-- (e.g., CREATE TEMP TABLE, DROP TABLE) via SPI.  Only the REFRESH itself
-- should be captured, with its full query text; the internal statements must
-- not be captured or clear the captured text.
CREATE TABLE base_tbl (id INT PRIMARY KEY, v TEXT);
CREATE MATERIALIZED VIEW mv AS SELECT * FROM base_tbl;
CREATE UNIQUE INDEX mv_idx ON mv (id);
REFRESH MATERIALIZED VIEW CONCURRENTLY mv;

SELECT yb_data FROM public.TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
select * from TEST_verify_replicated_ddls();

CALL TEST_reset();

-- DDLs run from inside DO blocks and procedures are real user DDLs; each is
-- captured with its own query text.
DO $$
BEGIN
  CREATE TABLE do_tbl1 (id INT);
  CREATE TABLE do_tbl2 (id INT);
END $$;

CREATE PROCEDURE create_tables() LANGUAGE plpgsql AS $$
BEGIN
  CREATE TABLE proc_tbl (id INT);
  CREATE INDEX proc_idx ON proc_tbl (id);
END $$;
CALL create_tables();

-- REFRESH MATERIALIZED VIEW CONCURRENTLY run from inside a procedure.  The
-- REFRESH is captured; its internal statements are still ignored.
CREATE PROCEDURE refresh_mv() LANGUAGE plpgsql AS $$
BEGIN
  REFRESH MATERIALIZED VIEW CONCURRENTLY mv;
END $$;
CALL refresh_mv();

SELECT yb_data FROM public.TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
select * from TEST_verify_replicated_ddls();

CALL TEST_reset();

-- Extension DDLs are captured as a single statement; the member DDLs that the
-- extension script runs are not captured separately.
CREATE EXTENSION pgcrypto;
DROP EXTENSION pgcrypto;

-- Same when the extension DDL itself is nested inside a DO block.
DO $$
BEGIN
  CREATE EXTENSION pgcrypto;
END $$;
DROP EXTENSION pgcrypto;

SELECT yb_data FROM public.TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
select * from TEST_verify_replicated_ddls();
