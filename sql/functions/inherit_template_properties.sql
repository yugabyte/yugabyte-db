CREATE FUNCTION inherit_template_properties (p_parent_table text, p_child_schema text, p_child_tablename text) RETURNS boolean
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE

v_child_relkind         char;
v_child_schema          text;
v_child_tablename       text;
v_fk_list               record;
v_index_list            record;
v_parent_oid            oid;
v_parent_table          text;
v_sql                   text;
v_template_oid          oid;
v_template_table        text;

BEGIN
/*
 * Function to inherit the properties of the template table to newly created child tables.
 * Currently used for PostgreSQL 10 to inherit indexes and FKs since that is not natively available
 */

SELECT parent_table, template_table
INTO v_parent_table, v_template_table
FROM @extschema@.part_config
WHERE parent_table = p_parent_table;
IF v_parent_table IS NULL THEN
    RAISE EXCEPTION 'Given parent table has no configuration in pg_partman: %', p_parent_table;
ELSIF v_template_table IS NULL THEN
    RAISE EXCEPTION 'No template table set in configuration for given parent table: %', p_parent_table;
END IF;
 
SELECT c.oid INTO v_parent_oid
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(p_parent_table, '.', 1)::name
AND c.relname = split_part(p_parent_table, '.', 2)::name;
    IF v_parent_oid IS NULL THEN
        RAISE EXCEPTION 'Unable to find given parent table in system catalogs: %', p_parent_table;
    END IF;
 
SELECT n.nspname, c.relname, c.relkind INTO v_child_schema, v_child_tablename, v_child_relkind
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = p_child_schema::name
AND c.relname = p_child_tablename::name;
    IF v_child_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find given child table in system catalogs: %.%', v_child_schema, v_child_tablename;
    END IF;
       
IF v_child_relkind = 'p' THEN
    -- Subpartitioned parent, do not apply properties
    RAISE DEBUG 'inherit_template_properties: found given child is subpartition parent, so properties not inherited';
    RETURN false;
END IF;

SELECT c.oid INTO v_template_oid
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = split_part(v_template_table, '.', 1)::name
AND c.relname = split_part(v_template_table, '.', 2)::name;
    IF v_child_tablename IS NULL THEN
        RAISE EXCEPTION 'Unable to find configured template table in system catalogs: %', v_template_table;
    END IF;

-- Index creation
FOR v_index_list IN 
    SELECT
    array_to_string(regexp_matches(pg_get_indexdef(indexrelid), ' USING .*'),',') AS statement
    , i.indisprimary
    , ( SELECT array_agg( a.attname ORDER by x.r )
        FROM pg_catalog.pg_attribute a
        JOIN ( SELECT k, row_number() over () as r
                FROM unnest(i.indkey) k ) as x
        ON a.attnum = x.k AND a.attrelid = i.indrelid
    ) AS indkey_names
    , c.relname AS index_name
    FROM pg_catalog.pg_index i
    JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
    WHERE i.indrelid = v_template_oid
    AND i.indisvalid
    ORDER BY 1
LOOP

    IF v_index_list.indisprimary THEN
        v_sql := format('ALTER TABLE %I.%I ADD PRIMARY KEY (%s)'
                        , v_child_schema
                        , v_child_tablename
                        , '"' || array_to_string(v_index_list.indkey_names, '","') || '"');
        RAISE DEBUG 'Create pk: %', v_sql;
        EXECUTE v_sql;
    ELSE
        -- statement column should be just the portion of the index definition that defines what it actually is
        v_sql := format('CREATE INDEX ON %I.%I %s', v_child_schema, v_child_tablename, v_index_list.statement);
        RAISE DEBUG 'Create index: %', v_sql;
        EXECUTE v_sql;

    END IF;

END LOOP;
-- End index creation

-- Foreign key creation
FOR v_fk_list IN 
    SELECT pg_get_constraintdef(con.oid) AS constraint_def
    FROM pg_catalog.pg_constraint con
    JOIN pg_catalog.pg_class c ON con.conrelid = c.oid
    WHERE c.oid = v_template_oid
    AND contype = 'f'
LOOP
    v_sql := format('ALTER TABLE %I.%I ADD %s', v_child_schema, v_child_tablename, v_fk_list.constraint_def);
    RAISE DEBUG 'Create FK: %', v_sql;
    EXECUTE v_sql;
END LOOP;
-- End foreign key creation

RETURN true;

END
$$;


