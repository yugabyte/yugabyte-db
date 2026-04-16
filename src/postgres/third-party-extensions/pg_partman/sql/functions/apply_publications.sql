CREATE FUNCTION @extschema@.apply_publications(p_parent_table text, p_child_schema text, p_child_tablename text) RETURNS void
    LANGUAGE plpgsql 
AS $$
DECLARE
    v_publications      text[];
    v_row               record;
    v_sql               text;
    yb_v_table_exists   boolean;
BEGIN
/*
* Function to ATLER PUBLICATION ... ADD TABLE to support logical replication
*/

SELECT c.publications INTO v_publications
FROM @extschema@.part_config c
WHERE c.parent_table = p_parent_table;

-- Loop over all publicaions which the table needs to be added to
FOR v_row IN
    SELECT pubname FROM unnest(v_publications) AS pubname
LOOP

    SELECT EXISTS (
        SELECT 1
        FROM pg_publication_rel pr
        JOIN pg_class c ON pr.prrelid = c.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        JOIN pg_publication p ON pr.prpubid = p.oid
        WHERE c.relname = p_child_tablename
        AND n.nspname = p_child_schema
        AND p.pubname = v_row.pubname
    ) INTO yb_v_table_exists;

    IF NOT yb_v_table_exists THEN
        v_sql = format('ALTER PUBLICATION %I ADD TABLE %I.%I', v_row.pubname, p_child_schema, p_child_tablename);
        RAISE DEBUG '%', v_sql;
        EXECUTE v_sql;
    END IF;
END LOOP;

END;
$$;

