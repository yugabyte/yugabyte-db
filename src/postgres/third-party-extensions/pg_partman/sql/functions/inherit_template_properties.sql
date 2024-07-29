CREATE FUNCTION @extschema@.inherit_template_properties (p_parent_table text, p_child_schema text, p_child_tablename text) RETURNS boolean
    LANGUAGE plpgsql 
    AS $$
DECLARE

v_child_relkind         char;
v_child_schema          text;
v_child_tablename       text;
v_child_unlogged        char;
v_dupe_found            boolean := false;
v_fk_list               record;
v_index_list            record;
v_inherit_fk            boolean;
v_parent_index_list     record;
v_parent_oid            oid;
v_parent_table          text;
v_relopt                record;
v_sql                   text;
v_template_oid          oid;
v_template_schemaname   text;
v_template_table        text;
v_template_tablename    name;
v_template_tablespace   name;
v_template_unlogged     char;
yb_v_child_index_found  boolean := false;
yb_v_child_index_list   record;

BEGIN
/*
 * Function to inherit the properties of the template table to newly created child tables.
 * Currently used for PostgreSQL 10 to inherit indexes and FKs since that is not natively available
 * For PG11, used to inherit non-partition-key unique indexes & primary keys
 */

SELECT parent_table, template_table, inherit_fk
INTO v_parent_table, v_template_table, v_inherit_fk
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

v_template_schemaname := split_part(v_template_table, '.', 1)::name;
v_template_tablename :=  split_part(v_template_table, '.', 2)::name;

SELECT c.oid, ts.spcname INTO v_template_oid, v_template_tablespace
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
LEFT OUTER JOIN pg_catalog.pg_tablespace ts ON c.reltablespace = ts.oid
WHERE n.nspname = v_template_schemaname
AND c.relname = v_template_tablename;
    IF v_template_oid IS NULL THEN
        RAISE EXCEPTION 'Unable to find configured template table in system catalogs: %', v_template_table;
    END IF;

-- Index creation (Required for all indexes in PG10. Only for non-unique, non-partition key indexes in PG11)
IF current_setting('server_version_num')::int >= 100000 THEN
    FOR v_index_list IN 
        SELECT
        array_to_string(regexp_matches(pg_get_indexdef(indexrelid), ' USING .*'),',') AS statement
        , i.indisprimary
        , i.indisunique
        , ( SELECT array_agg( a.attname ORDER by x.r )
            FROM pg_catalog.pg_attribute a
            JOIN ( SELECT k, row_number() over () as r
                    FROM unnest(i.indkey) k ) as x
            ON a.attnum = x.k AND a.attrelid = i.indrelid
        ) AS indkey_names
        , c.relname AS index_name
        , ts.spcname AS tablespace_name
        FROM pg_catalog.pg_index i
        JOIN pg_catalog.pg_class c ON i.indexrelid = c.oid
        LEFT OUTER JOIN pg_catalog.pg_tablespace ts ON c.reltablespace = ts.oid
        WHERE i.indrelid = v_template_oid
        AND i.indisvalid
        ORDER BY 1
    LOOP
        v_dupe_found := false;
        yb_v_child_index_found := false;

        IF current_setting('server_version_num')::int >= 110000 THEN
            FOR v_parent_index_list IN 
                SELECT
                array_to_string(regexp_matches(pg_get_indexdef(indexrelid), ' USING .*'),',') AS statement
                , i.indisprimary
                , ( SELECT array_agg( a.attname ORDER by x.r )
                    FROM pg_catalog.pg_attribute a
                    JOIN ( SELECT k, row_number() over () as r
                            FROM unnest(i.indkey) k ) as x
                    ON a.attnum = x.k AND a.attrelid = i.indrelid
                ) AS indkey_names
                FROM pg_catalog.pg_index i
                WHERE i.indrelid = v_parent_oid
                AND i.indisvalid
                ORDER BY 1
            LOOP

                IF v_parent_index_list.indisprimary AND v_index_list.indisprimary THEN
                    IF v_parent_index_list.indkey_names = v_index_list.indkey_names THEN
                        RAISE DEBUG 'inherit_template_properties: Ignoring duplicate primary key on template table: % ', v_index_list.indkey_names;
                        v_dupe_found := true;
                        CONTINUE; -- only continue within this nested loop
                    END IF;
                END IF;

                IF v_parent_index_list.statement = v_index_list.statement THEN
                    RAISE DEBUG 'inherit_template_properties: Ignoring duplicate index on template table: %', v_index_list.statement;
                    v_dupe_found := true;
                    CONTINUE; -- only continue within this nested loop
                END IF;

            END LOOP; -- end parent index loop
        END IF; -- End PG11 check

        IF v_dupe_found = true THEN
            -- Only used in PG11 and should skip trying to create indexes that already existed on the parent
            CONTINUE;
        END IF;

        -- YB: Check for existing index on child table
        FOR yb_v_child_index_list IN
            SELECT
            array_to_string(regexp_matches(pg_get_indexdef(indexrelid), ' USING .*'),',') AS statement
            , i.indisprimary
            , ( SELECT array_agg(a.attname ORDER by x.r)
                FROM pg_catalog.pg_attribute a
                JOIN ( SELECT k, row_number() over () as r
                        FROM unnest(i.indkey) k ) as x
                ON a.attnum = x.k AND a.attrelid = i.indrelid
            ) AS indkey_names
            FROM pg_catalog.pg_index i
            WHERE i.indrelid = ( SELECT oid FROM pg_catalog.pg_class WHERE relname = p_child_tablename AND relnamespace = ( SELECT oid FROM pg_catalog.pg_namespace WHERE nspname = p_child_schema ))
            AND i.indisvalid
            ORDER BY 1
        LOOP
            IF yb_v_child_index_list.indisprimary = v_index_list.indisprimary THEN
                IF yb_v_child_index_list.indisprimary THEN
                    IF yb_v_child_index_list.indkey_names = v_index_list.indkey_names THEN
                        RAISE DEBUG 'inherit_template_properties: Duplicate primary key found on child table: %', v_index_list.indkey_names;
                        yb_v_child_index_found := true;
                        CONTINUE; -- skip creating this index
                    END IF;
                END IF;
            END IF;

            IF yb_v_child_index_list.statement = v_index_list.statement THEN
                RAISE DEBUG 'inherit_template_properties: Duplicate index found on child table: %', v_index_list.statement;
                yb_v_child_index_found := true;
                CONTINUE; -- skip creating this index
            END IF;
        END LOOP;

        IF yb_v_child_index_found THEN
            CONTINUE;
        END IF;

        IF v_index_list.indisprimary THEN
            v_sql := format('ALTER TABLE %I.%I ADD PRIMARY KEY (%s)'
                            , v_child_schema
                            , v_child_tablename
                            , '"' || array_to_string(v_index_list.indkey_names, '","') || '"');
            IF v_index_list.tablespace_name IS NOT NULL THEN
                v_sql := v_sql || format(' USING INDEX TABLESPACE %I', v_index_list.tablespace_name);
            END IF;
            RAISE DEBUG 'inherit_template_properties: Create pk: %', v_sql;
            EXECUTE v_sql;
        ELSE
            -- statement column should be just the portion of the index definition that defines what it actually is
            v_sql := format('CREATE %s INDEX ON %I.%I %s', CASE WHEN v_index_list.indisunique = TRUE THEN 'UNIQUE' ELSE '' END, v_child_schema, v_child_tablename, v_index_list.statement);
            IF v_index_list.tablespace_name IS NOT NULL THEN
                v_sql := v_sql || format(' TABLESPACE %I', v_index_list.tablespace_name);
            END IF;

            RAISE DEBUG 'inherit_template_properties: Create index: %', v_sql;
            EXECUTE v_sql;

        END IF;

    END LOOP;
END IF; 
-- End index creation

-- Foreign key creation (PG10 only)
IF current_setting('server_version_num')::int >= 100000 AND current_setting('server_version_num')::int < 110000 THEN
    IF v_inherit_fk THEN
        FOR v_fk_list IN 
            SELECT pg_get_constraintdef(con.oid) AS constraint_def
            FROM pg_catalog.pg_constraint con
            JOIN pg_catalog.pg_class c ON con.conrelid = c.oid
            WHERE c.oid = v_template_oid
            AND contype = 'f'
        LOOP
            v_sql := format('ALTER TABLE %I.%I ADD %s', v_child_schema, v_child_tablename, v_fk_list.constraint_def);
            RAISE DEBUG 'inherit_template_properties: Create FK: %', v_sql;
            EXECUTE v_sql;
        END LOOP;
    END IF;
END IF;
-- End foreign key creation

-- Tablespace inheritance on PG11 and earlier
IF current_setting('server_version_num')::int < 120000 AND v_template_tablespace IS NOT NULL THEN
    v_sql := format('ALTER TABLE %I.%I SET TABLESPACE %I', v_child_schema, v_child_tablename, v_template_tablespace);
    RAISE DEBUG 'inherit_template_properties: Alter tablespace: %', v_sql;
    EXECUTE v_sql;
END IF;

-- UNLOGGED status. Currently waiting on final stance of how native will handle this property being changed for its children. 
-- See release notes for v4.2.0
SELECT relpersistence INTO v_template_unlogged
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = v_template_schemaname
AND c.relname = v_template_tablename;

SELECT relpersistence INTO v_child_unlogged
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = v_child_schema::name
AND c.relname = v_child_tablename::name;

IF v_template_unlogged = 'u' AND v_child_unlogged = 'p'  THEN
    v_sql := format ('ALTER TABLE %I.%I SET UNLOGGED', v_child_schema, v_child_tablename);
    RAISE DEBUG 'inherit_template_properties: Alter UNLOGGED: %', v_sql;
    EXECUTE v_sql;     
ELSIF v_template_unlogged = 'p' AND v_child_unlogged = 'u'  THEN
    v_sql := format ('ALTER TABLE %I.%I SET LOGGED', v_child_schema, v_child_tablename);
    RAISE DEBUG 'inherit_template_properties: Alter UNLOGGED: %', v_sql;
    EXECUTE v_sql;     
END IF;

-- Relation options are not being inherited for PG <= 13
FOR v_relopt IN
    SELECT unnest(reloptions) as value
    FROM pg_catalog.pg_class
    WHERE oid = v_template_oid
LOOP
    v_sql := format('ALTER TABLE %I.%I SET (%s)'
                    , v_child_schema
                    , v_child_tablename
                    , v_relopt.value);
    RAISE DEBUG 'inherit_template_properties: Set relopts: %', v_sql;
    EXECUTE v_sql;
END LOOP;
RETURN true;

END
$$;

