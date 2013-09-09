/*
 * Function to check uniqueness of a column in a partiton set.
 * First draft that runs within database in a single transaction. 
 * Working on version that will dump data out to perform a quicker check with less impact on DB.
 */
CREATE FUNCTION check_unique_column(p_parent_table text, p_column text) RETURNS SETOF check_unique_table
    LANGUAGE plpgsql
    AS $$
DECLARE

    v_row       record;
    v_sql       text;
    v_trouble   @extschema@.check_unique_table%rowtype;

BEGIN

v_sql := 'SELECT '||p_column||'::text AS column_value, count('||p_column||') AS count
        FROM '||p_parent_table||' GROUP BY '||p_column||' HAVING (count('||p_column||') > 1) ORDER BY '||p_column;

RAISE NOTICE 'v_sql: %', v_sql;

FOR v_row IN EXECUTE v_sql
LOOP
    v_trouble.column_value := v_row.column_value;
    v_trouble.count := v_row.count;
    RETURN NEXT v_trouble;
END LOOP;

END
$$;
