--
-- GH-30793: Tests that QPM and pg_stat_statements report identical execution times.
--

CREATE TABLE qpm_timing_test (id INT PRIMARY KEY, val INT);
INSERT INTO qpm_timing_test SELECT i, i * 10 FROM generate_series(1, 10) AS i;

-- Test 1: Top-level read query.
SET yb_pg_stat_plans_track = 'top';
SET yb_pg_stat_plans_track_catalog_queries = OFF;

-- The number of plans reset vary between debug and release builds. Therefore discard the result.
DO $$ BEGIN PERFORM yb_pg_stat_plans_reset(NULL, NULL, NULL, NULL); END $$;
SELECT pg_stat_statements_reset();

SELECT val FROM qpm_timing_test WHERE id = 1;

-- Verify QPM avg_exec_time matches PGSS mean_exec_time exactly.
SELECT
    pgss.query,
    (qpm.avg_exec_time = pgss.mean_exec_time) AS times_match,
    (qpm.avg_exec_time > 0) AS time_positive,
    qpm.calls
FROM yb_pg_stat_plans qpm
JOIN pg_stat_statements pgss USING (queryid)
ORDER BY pgss.queryid ASC
/* __YB_STAT_PLANS_SKIP */;

-- Same query run 5 times total to validate Welford's running mean.
SELECT val FROM qpm_timing_test WHERE id = 2;
SELECT val FROM qpm_timing_test WHERE id = 3;
SELECT val FROM qpm_timing_test WHERE id = 4;
SELECT val FROM qpm_timing_test WHERE id = 5;

-- Verify times still match after multiple calls.
SELECT
    pgss.query,
    (qpm.avg_exec_time = pgss.mean_exec_time) AS times_match,
    (qpm.avg_exec_time > 0) AS time_positive,
    qpm.calls
FROM yb_pg_stat_plans qpm
JOIN pg_stat_statements pgss USING (queryid)
ORDER BY pgss.queryid ASC
/* __YB_STAT_PLANS_SKIP */;

-- Test 2: Top-level catalog query with yb_pg_stat_plans_track_catalog_queries ON.
SET yb_pg_stat_plans_track_catalog_queries = ON;

SELECT relname FROM pg_class WHERE relname = 'pg_class' AND relkind = 'r';

-- Verify QPM avg_exec_time matches PGSS mean_exec_time exactly.
SELECT
    pgss.query,
    (qpm.avg_exec_time = pgss.mean_exec_time) AS times_match,
    (qpm.avg_exec_time > 0) AS time_positive,
    qpm.calls
FROM yb_pg_stat_plans qpm
JOIN pg_stat_statements pgss USING (queryid)
ORDER BY pgss.queryid ASC
/* __YB_STAT_PLANS_SKIP */;

-- Test 3: Non-top-level read query with track='all'.
SET yb_pg_stat_plans_track_catalog_queries = OFF;
SET yb_pg_stat_plans_track = 'all';
SET pg_stat_statements.track = 'all';

CREATE OR REPLACE FUNCTION qpm_inner_select() RETURNS bigint LANGUAGE plpgsql AS $$
DECLARE
    r bigint;
BEGIN
    SELECT count(*) INTO r FROM qpm_timing_test;
    RETURN r;
END;
$$;

SELECT qpm_inner_select();

-- Verify that the non-top-level inner SELECT timing matches PGSS.
SELECT
    pgss.query,
    (qpm.avg_exec_time = pgss.mean_exec_time) AS times_match,
    (qpm.avg_exec_time > 0) AS time_positive,
    qpm.calls
FROM yb_pg_stat_plans qpm
JOIN pg_stat_statements pgss USING (queryid)
ORDER BY pgss.queryid ASC
/* __YB_STAT_PLANS_SKIP */;

-- Cleanup
DROP FUNCTION qpm_inner_select();
DROP TABLE qpm_timing_test;

--
-- Test to validate NULL inputs to yb_pg_stat_plans_insert
--
DO $$ BEGIN PERFORM yb_pg_stat_plans_reset(NULL, NULL, NULL, NULL); END $$;

-- The following is a valid entry.
SELECT yb_pg_stat_plans_insert(
    1, -- dbid
    2, -- user_id
    3, -- query_id
    4, -- plan_id
    'Valid hint text', -- hint_text
    'Valid plan text', -- plan_text
    '2025-01-01 14:00:00+00', -- first_used
    '2025-01-02 14:00:00+00', -- last_used
    1.0, -- total_time
    1.0 -- est_total_cost
) /* __YB_STAT_PLANS_SKIP */;

-- dbid cannot be NULL
SELECT yb_pg_stat_plans_insert(
    NULL, -- dbid
    2, -- user_id
    3, -- query_id
    4, -- plan_id
    'Valid hint text', -- hint_text
    'Valid plan text', -- plan_text
    '2025-01-01 14:00:00+00', -- first_used
    '2025-01-02 14:00:00+00', -- last_used
    1.0, -- total_time
    1.0 -- est_total_cost
) /* __YB_STAT_PLANS_SKIP */;

-- user_id cannot be NULL
SELECT yb_pg_stat_plans_insert(
    1, -- dbid
    NULL, -- user_id
    3, -- query_id
    4, -- plan_id
    'Valid hint text', -- hint_text
    'Valid plan text', -- plan_text
    '2025-01-01 14:00:00+00', -- first_used
    '2025-01-02 14:00:00+00', -- last_used
    1.0, -- total_time
    1.0 -- est_total_cost
) /* __YB_STAT_PLANS_SKIP */;

-- query_id cannot be NULL
SELECT yb_pg_stat_plans_insert(
    1, -- dbid
    2, -- user_id
    NULL, -- query_id
    4, -- plan_id
    'Valid hint text', -- hint_text
    'Valid plan text', -- plan_text
    '2025-01-01 14:00:00+00', -- first_used
    '2025-01-02 14:00:00+00', -- last_used
    1.0, -- total_time
    1.0 -- est_total_cost
) /* __YB_STAT_PLANS_SKIP */;

-- plan_id cannot be NULL
SELECT yb_pg_stat_plans_insert(
    1, -- dbid
    2, -- user_id
    3, -- query_id
    NULL, -- plan_id
    'Valid hint text', -- hint_text
    'Valid plan text', -- plan_text
    '2025-01-01 14:00:00+00', -- first_used
    '2025-01-02 14:00:00+00', -- last_used
    1.0, -- total_time
    1.0 -- est_total_cost
) /* __YB_STAT_PLANS_SKIP */;

-- Hint text cannot be NULL
SELECT yb_pg_stat_plans_insert(
    1, -- dbid
    2, -- user_id
    3, -- query_id
    4, -- plan_id
    NULL, -- hint_text
    'Valid plan text', -- plan_text
    '2025-01-01 14:00:00+00', -- first_used
    '2025-01-02 14:00:00+00', -- last_used
    1.0, -- total_time
    1.0 -- est_total_cost
) /* __YB_STAT_PLANS_SKIP */;

-- Plan text cannot be NULL
SELECT yb_pg_stat_plans_insert(
    1, -- dbid
    2, -- user_id
    3, -- queryid
    4, -- plan_id
    'Valid hint text', -- hint_text
    NULL, -- plan_text
    '2025-01-01 14:00:00+00', -- first_used
    '2025-01-02 14:00:00+00', -- last_used
    1.0, -- total_time
    1.0 -- est_total_cost
) /* __YB_STAT_PLANS_SKIP */;

-- Stats must be populated as the function is marked as STRICT
SELECT yb_pg_stat_plans_insert(
    1, -- dbid
    2, -- user_id
    3, -- queryid
    4, -- plan_id
    'Valid hint text', -- hint_text
    'Valid plan text', -- plan_text
    NULL, -- first_used
    NULL, -- last_used
    NULL, -- total_time
    NULL -- est_total_cost
) /* __YB_STAT_PLANS_SKIP */;

SELECT * FROM yb_pg_stat_plans /* __YB_STAT_PLANS_SKIP */;
