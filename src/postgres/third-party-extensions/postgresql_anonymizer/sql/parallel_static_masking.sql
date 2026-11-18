
SET search_path='';

CREATE OR REPLACE FUNCTION anon.get_related_tables(start_table_oid OID)
RETURNS TABLE (
    table_oid OID
)
LANGUAGE sql
AS
$$
WITH RECURSIVE
    is_masked_oid AS (
        SELECT
            c.oid AS relid,       -- Table OID
            a.attnum AS attnum    -- Column Attribute Number
        FROM anon.pg_masking_rules mr
        INNER JOIN pg_class c ON c.relname = mr.relname
        INNER JOIN pg_attribute a ON a.attrelid = c.oid AND a.attname = mr.attname
    ),
    children AS (

        SELECT
            conrelid AS related_oid,
            ARRAY[start_table_oid, conrelid] AS path -- Track the full path
        FROM pg_constraint c
        INNER JOIN pg_attribute a_fk ON a_fk.attrelid = c.conrelid AND a_fk.attnum = c.conkey[1] -- Child FK column
        INNER JOIN pg_attribute a_pk ON a_pk.attrelid = c.confrelid AND a_pk.attnum = c.confkey[1] -- Parent PK column
        WHERE c.confrelid = start_table_oid
            AND c.contype = 'f' -- Ensure it is a Foreign Key
            AND (
                -- Option A: Check if the FK column in the child table is masked
                EXISTS (
                    SELECT 1 FROM is_masked_oid
                    WHERE is_masked_oid.relid = c.conrelid
                    AND is_masked_oid.attnum = a_fk.attnum
                )
                OR
                -- Option B: Check if the referenced PK column in the parent table is masked
                EXISTS (
                    SELECT 1 FROM is_masked_oid
                    WHERE is_masked_oid.relid = c.confrelid
                    AND is_masked_oid.attnum = a_pk.attnum
                )
            )

        UNION ALL

        SELECT
            c.conrelid,
            c.conrelid || children.path -- Append the new OID to the path
        FROM pg_constraint c
        INNER JOIN children ON c.confrelid = children.related_oid
        INNER JOIN pg_attribute a_fk ON a_fk.attrelid = c.conrelid AND a_fk.attnum = c.conkey[1]
        INNER JOIN pg_attribute a_pk ON a_pk.attrelid = c.confrelid AND a_pk.attnum = c.confkey[1]
        WHERE c.contype = 'f'
            AND NOT (c.conrelid = ANY(children.path))
            AND (
                EXISTS (
                    SELECT 1 FROM is_masked_oid
                    WHERE is_masked_oid.relid = c.conrelid
                    AND is_masked_oid.attnum = a_fk.attnum
                )
                OR
                EXISTS (
                    SELECT 1 FROM is_masked_oid
                    WHERE is_masked_oid.relid = c.confrelid
                    AND is_masked_oid.attnum = a_pk.attnum
                )
            )

    ),

    parents AS (
        SELECT
            confrelid AS related_oid,
            ARRAY[start_table_oid, confrelid] AS path -- Track the full path
        FROM pg_constraint c
        INNER JOIN pg_attribute a_fk ON a_fk.attrelid = c.conrelid AND a_fk.attnum = c.conkey[1]
        INNER JOIN pg_attribute a_pk ON a_pk.attrelid = c.confrelid AND a_pk.attnum = c.confkey[1]
        WHERE c.conrelid = start_table_oid
            AND c.contype = 'f'
            AND (
                EXISTS (
                    SELECT 1 FROM is_masked_oid
                    WHERE is_masked_oid.relid = c.conrelid
                    AND is_masked_oid.attnum = a_fk.attnum
                )
                OR
                EXISTS (
                    SELECT 1 FROM is_masked_oid
                    WHERE is_masked_oid.relid = c.confrelid
                    AND is_masked_oid.attnum = a_pk.attnum
                )
            )


        UNION ALL

        SELECT
            c.confrelid,
            c.confrelid || parents.path -- Append the new OID to the path
        FROM pg_constraint c
        INNER JOIN parents ON c.conrelid = parents.related_oid
        INNER JOIN pg_attribute a_fk ON a_fk.attrelid = c.conrelid AND a_fk.attnum = c.conkey[1]
        INNER JOIN pg_attribute a_pk ON a_pk.attrelid = c.confrelid AND a_pk.attnum = c.confkey[1]
        WHERE c.contype = 'f'
            AND NOT (c.confrelid = ANY(parents.path))
            AND (
                EXISTS (
                    SELECT 1 FROM is_masked_oid
                    WHERE is_masked_oid.relid = c.conrelid
                    AND is_masked_oid.attnum = a_fk.attnum
                )
                OR
                EXISTS (
                    SELECT 1 FROM is_masked_oid
                    WHERE is_masked_oid.relid = c.confrelid
                    AND is_masked_oid.attnum = a_pk.attnum
                )
            )
    ),

    all_related_oids AS (
        SELECT related_oid FROM children
        UNION
        SELECT related_oid FROM parents
        UNION
        SELECT start_table_oid
    )

SELECT DISTINCT
    r.related_oid AS table_oid
FROM all_related_oids r
INNER JOIN pg_class ON pg_class.oid = r.related_oid
INNER JOIN pg_namespace ON pg_namespace.oid = pg_class.relnamespace
WHERE pg_class.relkind = 'r'
AND pg_namespace.nspname NOT IN ('pg_catalog', 'information_schema', 'anon');
$$;

CREATE OR REPLACE FUNCTION anon.dispatch(
    worker_size integer,
    oids oid[]
) RETURNS TABLE (group_id integer, list_oid oid[])
AS
$$
DECLARE
    i integer;
    gr_id integer := 0;
    oid_rec RECORD;
    elt RECORD;
BEGIN
    IF worker_size <= 0 THEN
        RAISE EXCEPTION 'worker_size must be greater than 0';
    END IF;
    CREATE TEMP TABLE IF NOT EXISTS temp_oids(oid oid, row_count integer) ON COMMIT DROP;
    TRUNCATE temp_oids;
    INSERT INTO temp_oids SELECT unnest(oids);
    UPDATE temp_oids t
    SET row_count = c.reltuples
    FROM pg_class c
    WHERE t.oid = c.oid;

    CREATE TEMP TABLE IF NOT EXISTS temp_related_oids(gid integer, related_oid oid) ON COMMIT DROP;
    TRUNCATE temp_related_oids;

    FOR oid_rec IN SELECT oid FROM temp_oids ORDER BY row_count DESC LOOP
        gr_id := gr_id + 1;
        IF EXISTS (SELECT 1 FROM temp_related_oids WHERE related_oid = oid_rec.oid) THEN
            CONTINUE;
        END IF;

        INSERT INTO temp_related_oids (gid, related_oid)
        SELECT gr_id, table_oid from anon.get_related_tables(oid_rec.oid)
        UNION
        SELECT gr_id, oid_rec.oid
        WHERE NOT EXISTS (
            SELECT 1 FROM anon.get_related_tables(oid_rec.oid)
        );
    END LOOP;

    RETURN QUERY
    WITH numbered AS (
    SELECT
        gid,
        array_agg(related_oid ORDER BY related_oid) AS list_oid,
        row_number() OVER (ORDER BY gid) AS rn,
        count(*) OVER () AS total_groups
    FROM temp_related_oids
    GROUP BY gid
    ), unflattened AS (
    SELECT
    (floor((rn - 1) * worker_size::numeric / total_groups) + 1)::integer AS worker_group,
    array_agg(oid ORDER BY rn) AS oids
    FROM (
        SELECT
            n.rn,
            n.total_groups,
            unnest(n.list_oid) AS oid
        FROM numbered n
    ) sub
    GROUP BY worker_group
    ORDER BY worker_group),
    flattened AS (
    SELECT
        uf.worker_group AS group_id,
        unnest(uf.oids) AS oid
    FROM unflattened uf
    )
    SELECT
    f.group_id,
    array_agg(f.oid ORDER BY f.oid) AS oids
    FROM flattened f
    GROUP BY f.group_id
    ORDER BY f.group_id;
END;
$$ LANGUAGE plpgsql;

RESET search_path;
