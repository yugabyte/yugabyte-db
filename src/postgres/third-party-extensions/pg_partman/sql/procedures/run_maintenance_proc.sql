CREATE PROCEDURE @extschema@.run_maintenance_proc(p_wait int DEFAULT 0, p_analyze boolean DEFAULT NULL, p_jobmon boolean DEFAULT true)
    LANGUAGE plpgsql
    AS $$
DECLARE

v_adv_lock              boolean;
v_row                   record;
v_sql                   text;
v_tables_list_sql       text;

BEGIN

/* YB(GH#3642): advisory lock not supported
v_adv_lock := pg_try_advisory_lock(hashtext('pg_partman run_maintenance'));
IF v_adv_lock = false THEN
    RAISE NOTICE 'Partman maintenance already running or another session has not released its advisory lock.';
    RETURN;
END IF;
*/

v_tables_list_sql := 'SELECT parent_table
            FROM @extschema@.part_config
            WHERE undo_in_progress = false
            AND automatic_maintenance = ''on''';
 
FOR v_row IN EXECUTE v_tables_list_sql
LOOP
/*
 * Run maintenance with a commit between each partition set
 * TODO - Once PG11 is more mainstream, see about more full conversion of run_maintenance function as well as turning 
 *        create_partition* functions into procedures to commit after every child table is made. May need to wait
 *        for more PROCEDURE features as well (return values, search_path, etc).
 *      - Also see about swapping names so this is the main object to call for maintenance instead of a function.
 */
    v_sql := format('SELECT %I.run_maintenance(%L, p_jobmon := %L',
        '@extschema@', v_row.parent_table, p_jobmon);

    IF p_analyze IS NOT NULL THEN
        v_sql := v_sql || format(', p_analyze := %L', p_analyze);
    END IF;
        
    v_sql := v_sql || ')';

    RAISE DEBUG 'v_sql run_maintenance_proc: %', v_sql;

    EXECUTE v_sql;
    COMMIT;

    PERFORM pg_sleep(p_wait);

END LOOP;

/* YB(GH#3642): advisory lock not supported
PERFORM pg_advisory_unlock(hashtext('pg_partman run_maintenance'));
*/
END
$$;

