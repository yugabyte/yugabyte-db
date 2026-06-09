-- This script is used to enable MotherDuck support if
-- the token is provided in the environment variables.

-- Once we stop supporting PG14 we can use \getenv again
-- For grepability: PG_VERSION_NUM < 150000
\set lc_token `echo $motherduck_token`
\set uc_token `echo $MOTHERDUCK_TOKEN`

SELECT
    LENGTH(:'lc_token') > 0 as lc_token_set,
    LENGTH(:'uc_token') > 0 as uc_token_set
\gset

\if :lc_token_set
    CALL duckdb.enable_motherduck(:'lc_token'::TEXT);
\elif :uc_token_set
    CALL duckdb.enable_motherduck(:'uc_token'::TEXT);
\else
    -- MotherDuck was not enabled, so we can skip the rest of the script
    \q
\endif

DO $$
DECLARE
    prev_count INTEGER := 0;
    curr_count INTEGER;
    total_count INTEGER := 0;
BEGIN
    -- Wait for the initial background worker startup for 5 seconds
    PERFORM pg_sleep(5);

    -- get initial count
    SELECT COUNT(*) INTO prev_count FROM duckdb.tables;

    -- Wait for another 25 seconds until the table count stabilizes (probably
    -- the sync is finished then)
    WHILE total_count < 25 LOOP
        PERFORM pg_sleep(1);
        total_count := total_count + 1;

        SELECT COUNT(*) INTO curr_count FROM duckdb.tables;

        IF curr_count = prev_count THEN
            -- count unchanged for 1 second, exit
            EXIT;
        END IF;

        prev_count := curr_count;
    END LOOP;
END;
$$;
