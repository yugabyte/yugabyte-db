-- Check transaction priority bounds.

set log_error_verbosity = default;

-- Values should be in interval [0,1] (inclusive).
-- Invalid values.
set yb_transaction_priority_upper_bound = 2;
set yb_transaction_priority_lower_bound = -1;

-- Valid values.
set yb_transaction_priority_upper_bound = 1;
set yb_transaction_priority_lower_bound = 0;
set yb_transaction_priority_lower_bound = 0.3;
set yb_transaction_priority_upper_bound = 0.7;

-- Lower bound should be less or equal to upper bound.
-- Invalid values.
set yb_transaction_priority_upper_bound = 0.2;
set yb_transaction_priority_lower_bound = 0.8;

-- Valid values.
set yb_transaction_priority_upper_bound = 0.3;
set yb_transaction_priority_upper_bound = 0.6;
set yb_transaction_priority_lower_bound = 0.4;
set yb_transaction_priority_lower_bound = 0.6;

-- Test yb_fetch_row_limit
set yb_fetch_row_limit = 100;
show yb_fetch_row_limit;

set yb_fetch_row_limit = -1;  -- ERROR since yb_fetch_row_limit must be non-negative.

-- Test yb_fetch_size_limit
set yb_fetch_size_limit = '2MB';
show yb_fetch_size_limit;
set yb_fetch_size_limit = 789;
show yb_fetch_size_limit;
set yb_fetch_size_limit = 2048;
show yb_fetch_size_limit;

set yb_fetch_size_limit = -1;  -- ERROR since yb_fetch_size_limit must be non-negative.

-- Check enable_seqscan, enable_indexscan, enable_indexonlyscan for YB scans.
CREATE TABLE test_scan (i int, j int);
CREATE INDEX NONCONCURRENTLY ON test_scan (j);

-- We want to know when disable_cost is added, but we don't want to depend on
-- the exact cost value.
CREATE OR REPLACE FUNCTION get_plan_details(stmt text) RETURNS TABLE (
    scan_type text,
    disabled boolean
) AS
$_$
DECLARE
    ret text;
    first_line text;
BEGIN
    EXECUTE format('EXPLAIN (FORMAT text) %s', stmt) INTO ret;
    first_line := split_part(ret, E'\n', 1); -- Extract the first line
    -- return (first two words of the line, is_disabled)
    RETURN QUERY
      SELECT trim(split_part(first_line, ' ', 1) || ' ' || split_part(first_line, ' ', 2)) AS scan_type,
      first_line SIMILAR TO '%cost=1[0-9]{10}%' AS disabled;
END;
$_$
LANGUAGE plpgsql;

set yb_enable_bitmapscan = on;
set enable_seqscan = on;
set enable_indexscan = on;
set enable_indexonlyscan = on;
set enable_bitmapscan = on;
SELECT * FROM get_plan_details('SELECT * FROM test_scan;');
SELECT * FROM get_plan_details('SELECT * FROM test_scan WHERE j = 1;');
SELECT * FROM get_plan_details('SELECT j FROM test_scan;');
set enable_seqscan = on;
set enable_indexscan = off;
set enable_bitmapscan = off;
SELECT * FROM get_plan_details('SELECT * FROM test_scan;');
SELECT * FROM get_plan_details('SELECT * FROM test_scan WHERE j = 1;');
SELECT * FROM get_plan_details('SELECT j FROM test_scan;');
set enable_seqscan = off;
set enable_indexscan = off;
set enable_bitmapscan = off;
SELECT * FROM get_plan_details('SELECT * FROM test_scan;');
SELECT * FROM get_plan_details('SELECT * FROM test_scan WHERE j = 1;');
SELECT * FROM get_plan_details('SELECT j FROM test_scan;');
set enable_seqscan = off;
set enable_indexscan = on;
set enable_bitmapscan = off;
SELECT * FROM get_plan_details('SELECT * FROM test_scan;');
SELECT * FROM get_plan_details('SELECT * FROM test_scan WHERE j = 1;');
SELECT * FROM get_plan_details('SELECT j FROM test_scan;');
set enable_seqscan = off;
set enable_indexscan = off;
set enable_bitmapscan = on;
SELECT * FROM get_plan_details('SELECT * FROM test_scan;');
SELECT * FROM get_plan_details('SELECT * FROM test_scan WHERE j = 1;');
SELECT * FROM get_plan_details('SELECT j FROM test_scan;');
set enable_indexonlyscan = off;
SELECT * FROM get_plan_details('SELECT j FROM test_scan;');

DROP FUNCTION get_plan_details;

-- Show transaction priority. As it is not possible to have a deterministic
-- yb_transaction_priority, we set yb_transaction_priority_lower_bound and
-- yb_transaction_priority_upper_bound to be the same, which forces
-- yb_transaction_priority to be equal to those two.
set yb_transaction_priority_lower_bound = 0.4;
set yb_transaction_priority_upper_bound = 0.4;
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
INSERT INTO test_scan (i, j) values (1, 1), (2, 2), (3, 3);
show yb_transaction_priority;
COMMIT;

-- Trying to set yb_transaction_priority will be an error
set yb_transaction_priority = 0.3; -- ERROR

-- High priority transaction
set yb_transaction_priority_lower_bound = 0.4;
set yb_transaction_priority_upper_bound = 0.4;
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT i, j FROM test_scan WHERE i = 1 FOR UPDATE;
show yb_transaction_priority;
COMMIT;

-- Highest priority transaction
set yb_transaction_priority_upper_bound = 1;
set yb_transaction_priority_lower_bound = 1;
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT i, j FROM test_scan WHERE i = 1 FOR UPDATE;
show yb_transaction_priority;
COMMIT;

-- Showing yb_transaction_priority outside a transaction block
show yb_transaction_priority;

-- SET LOCAL is restricted by a function SET option
create or replace function myfunc(int) returns text as $$
begin
  set local work_mem = '2MB';
  return current_setting('work_mem');
end $$
language plpgsql
set work_mem = '1MB';

select myfunc(0), current_setting('work_mem');

-- test SET unrecognized parameter
SET foo = false;  -- no such setting

-- test temp_file_limit default
SHOW temp_file_limit;
-- test temp_File_limit update
SET temp_file_limit="100MB";
SHOW temp_file_limit;
SET temp_file_limit=-1;
SHOW temp_file_limit;

-- test `yb_db_admin` role can set and reset yb_db_admin-allowed PGC_SUSET variables
SET SESSION AUTHORIZATION yb_db_admin;
SHOW session_replication_role;
SET session_replication_role TO replica;
SHOW session_replication_role;
RESET session_replication_role;
SHOW session_replication_role;
-- test `yb_db_admin` role cannot set and reset other PGC_SUSET variables
SET track_functions TO TRACK_FUNC_PL;
RESET track_functions;

-- cleanup
RESET foo;
RESET yb_enable_bitmapscan;
