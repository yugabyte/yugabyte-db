/*
 * Function to list all child partitions in a set.
 * Will list all child tables in any inheritance set, 
 * not just those managed by pg_partman.
 */
CREATE FUNCTION show_partitions (p_parent_table text) RETURNS SETOF text 
    LANGUAGE plpgsql STABLE SECURITY DEFINER
    AS $$
BEGIN

RETURN QUERY EXECUTE '
    SELECT n.nspname::text ||''.''|| c.relname::text AS partition_name FROM
    pg_catalog.pg_inherits h
    JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
    JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
    WHERE h.inhparent = '||quote_literal(p_parent_table)||'::regclass
    ORDER BY c.relname';
END
$$;

