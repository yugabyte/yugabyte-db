-- This update has no core code changes. It is being released to give users that ran into a situation where the trigger functions for serial partition sets called a function that was renamed. In v1.8.0 create_id_partition() was renamed to create_partition_id(). All core code was updated to use this new function, but any existing trigger function from previous versions may have the old function name in the code that creates new partitions when the current one reaches 50% of the max.
-- The code block below will go through and recreate all the trigger functions on all partition sets managed by pg_partman. This should fix the issue described above. While only serial partition sets should have been affected, the code below does it for both time & id to just ensure everything is fixed.
-- This code MUST must be run manually because if the trigger functions are recreated as part of an extension update, then those trigger functions become members of the pg_partman extension itself. Just copy-n-paste it into psql and things should be good to go.

/***********************************************************
-- Begin code to fix partition functions
-- You may need to adjust the setting of the search_path below to point the following commands to the schema that partman is installed to

SELECT set_config('search_path','partman, "$user", public',false);

CREATE TEMP TABLE partman_trigfix_temp (statement text);

INSERT INTO partman_trigfix_temp 
SELECT format('SELECT create_function_id(%L);', parent_table)
FROM part_config
WHERE type like 'id%';

INSERT INTO partman_trigfix_temp 
SELECT format('SELECT create_function_time(%L);', parent_table)
FROM part_config
WHERE type like 'time%';

DO $$
DECLARE
v_row   record;
BEGIN
    FOR v_row IN SELECT statement FROM partman_trigfix_temp LOOP
        IF v_row.statement IS NOT NULL THEN
            EXECUTE v_row.statement;
        END IF;
    END LOOP;
END
$$;

DROP TABLE IF EXISTS partman_trigfix_temp;

-- End code to fix partiton functions
************************************************************/


