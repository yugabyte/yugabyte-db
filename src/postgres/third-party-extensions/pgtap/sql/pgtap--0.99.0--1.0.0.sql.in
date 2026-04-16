CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 1.0;'
LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION _array_to_sorted_string( name[], text )
RETURNS text AS $$
    SELECT array_to_string(ARRAY(
        SELECT $1[i]
          FROM generate_series(1, array_upper($1, 1)) s(i)
         ORDER BY $1[i]
    ), $2);
$$ LANGUAGE SQL immutable;

-- policies_are( schema, table, policies[], description )
CREATE OR REPLACE FUNCTION policies_are( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'policies',
        ARRAY(
            SELECT p.polname
              FROM pg_catalog.pg_policy p
              JOIN pg_catalog.pg_class c     ON c.oid = p.polrelid
              JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
             WHERE n.nspname = $1
               AND c.relname = $2
            EXCEPT
            SELECT $3[i]
              FROM generate_series(1, array_upper($3, 1)) s(i)
        ),
        ARRAY(
            SELECT $3[i]
              FROM generate_series(1, array_upper($3, 1)) s(i)
            EXCEPT
            SELECT p.polname
              FROM pg_catalog.pg_policy p
              JOIN pg_catalog.pg_class c     ON c.oid = p.polrelid
              JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
             WHERE n.nspname = $1
               AND c.relname = $2
        ),
        $4
    );
$$ LANGUAGE SQL;

-- policies_are( schema, table, policies[] )
CREATE OR REPLACE FUNCTION policies_are( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT policies_are( $1, $2, $3, 'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should have the correct policies' );
$$ LANGUAGE SQL;

-- policies_are( table, policies[], description )
CREATE OR REPLACE FUNCTION policies_are( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'policies',
        ARRAY(
            SELECT p.polname
              FROM pg_catalog.pg_policy p
              JOIN pg_catalog.pg_class c ON c.oid = p.polrelid
              JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
             WHERE c.relname = $1
               AND n.nspname NOT IN ('pg_catalog', 'information_schema')
            EXCEPT
            SELECT $2[i]
              FROM generate_series(1, array_upper($2, 1)) s(i)
        ),
        ARRAY(
            SELECT $2[i]
              FROM generate_series(1, array_upper($2, 1)) s(i)
            EXCEPT
            SELECT p.polname
              FROM pg_catalog.pg_policy p
              JOIN pg_catalog.pg_class c ON c.oid = p.polrelid
              JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
               AND n.nspname NOT IN ('pg_catalog', 'information_schema')
        ),
        $3
    );
$$ LANGUAGE SQL;

-- policies_are( table, policies[] )
CREATE OR REPLACE FUNCTION policies_are( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT policies_are( $1, $2, 'Table ' || quote_ident($1) || ' should have the correct policies' );
$$ LANGUAGE SQL;

-- policy_roles_are( schema, table, policy, roles[], description )
CREATE OR REPLACE FUNCTION policy_roles_are( NAME, NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'policy roles',
        ARRAY(
            SELECT pr.rolname
              FROM pg_catalog.pg_policy AS pp
              JOIN pg_catalog.pg_roles AS pr ON pr.oid = ANY (pp.polroles)
              JOIN pg_catalog.pg_class AS pc ON pc.oid = pp.polrelid
              JOIN pg_catalog.pg_namespace AS pn ON pn.oid = pc.relnamespace
             WHERE pn.nspname = $1
               AND pc.relname = $2
               AND pp.polname = $3
            EXCEPT
            SELECT $4[i]
              FROM generate_series(1, array_upper($4, 1)) s(i)
        ),
        ARRAY(
            SELECT $4[i]
              FROM generate_series(1, array_upper($4, 1)) s(i)
            EXCEPT
            SELECT pr.rolname
              FROM pg_catalog.pg_policy AS pp
              JOIN pg_catalog.pg_roles AS pr ON pr.oid = ANY (pp.polroles)
              JOIN pg_catalog.pg_class AS pc ON pc.oid = pp.polrelid
              JOIN pg_catalog.pg_namespace AS pn ON pn.oid = pc.relnamespace
             WHERE pn.nspname = $1
               AND pc.relname = $2
               AND pp.polname = $3
        ),
        $5
    );
$$ LANGUAGE SQL;

-- policy_roles_are( schema, table, policy, roles[] )
CREATE OR REPLACE FUNCTION policy_roles_are( NAME, NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT policy_roles_are( $1, $2, $3, $4, 'Policy ' || quote_ident($3) || ' for table ' || quote_ident($1) || '.' || quote_ident($2) || ' should have the correct roles' );
$$ LANGUAGE SQL;

-- policy_roles_are( table, policy, roles[], description )
CREATE OR REPLACE FUNCTION policy_roles_are( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'policy roles',
        ARRAY(
            SELECT pr.rolname
              FROM pg_catalog.pg_policy AS pp
              JOIN pg_catalog.pg_roles AS pr ON pr.oid = ANY (pp.polroles)
              JOIN pg_catalog.pg_class AS pc ON pc.oid = pp.polrelid
              JOIN pg_catalog.pg_namespace AS pn ON pn.oid = pc.relnamespace
             WHERE pc.relname = $1
               AND pp.polname = $2
               AND pn.nspname NOT IN ('pg_catalog', 'information_schema')
            EXCEPT
            SELECT $3[i]
              FROM generate_series(1, array_upper($3, 1)) s(i)
        ),
        ARRAY(
            SELECT $3[i]
              FROM generate_series(1, array_upper($3, 1)) s(i)
            EXCEPT
            SELECT pr.rolname
              FROM pg_catalog.pg_policy AS pp
              JOIN pg_catalog.pg_roles AS pr ON pr.oid = ANY (pp.polroles)
              JOIN pg_catalog.pg_class AS pc ON pc.oid = pp.polrelid
              JOIN pg_catalog.pg_namespace AS pn ON pn.oid = pc.relnamespace
             WHERE pc.relname = $1
               AND pp.polname = $2
               AND pn.nspname NOT IN ('pg_catalog', 'information_schema')
        ),
        $4
    );
$$ LANGUAGE SQL;

-- policy_roles_are( table, policy, roles[] )
CREATE OR REPLACE FUNCTION policy_roles_are( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT policy_roles_are( $1, $2, $3, 'Policy ' || quote_ident($2) || ' for table ' || quote_ident($1) || ' should have the correct roles' );
$$ LANGUAGE SQL;

-- policy_cmd_is( schema, table, policy, command, description )
CREATE OR REPLACE FUNCTION policy_cmd_is( NAME, NAME, NAME, text, text )
RETURNS TEXT AS $$
DECLARE
    cmd text;
BEGIN
    SELECT
      CASE pp.polcmd WHEN 'r' THEN 'SELECT'
                     WHEN 'a' THEN 'INSERT'
                     WHEN 'w' THEN 'UPDATE'
                     WHEN 'd' THEN 'DELETE'
                     ELSE 'ALL'
       END
      FROM pg_catalog.pg_policy AS pp
      JOIN pg_catalog.pg_class AS pc ON pc.oid = pp.polrelid
      JOIN pg_catalog.pg_namespace AS pn ON pn.oid = pc.relnamespace
     WHERE pn.nspname = $1
       AND pc.relname = $2
       AND pp.polname = $3
      INTO cmd;

    RETURN is( cmd, upper($4), $5 );
END;
$$ LANGUAGE plpgsql;

-- policy_cmd_is( schema, table, policy, command )
CREATE OR REPLACE FUNCTION policy_cmd_is( NAME, NAME, NAME, text )
RETURNS TEXT AS $$
    SELECT policy_cmd_is(
        $1, $2, $3, $4,
        'Policy ' || quote_ident($3)
        || ' for table ' || quote_ident($1) || '.' || quote_ident($2)
        || ' should apply to ' || upper($4) || ' command'
    );
$$ LANGUAGE sql;

-- policy_cmd_is( table, policy, command, description )
CREATE OR REPLACE FUNCTION policy_cmd_is( NAME, NAME, text, text )
RETURNS TEXT AS $$
DECLARE
    cmd text;
BEGIN
    SELECT
      CASE pp.polcmd WHEN 'r' THEN 'SELECT'
                     WHEN 'a' THEN 'INSERT'
                     WHEN 'w' THEN 'UPDATE'
                     WHEN 'd' THEN 'DELETE'
                     ELSE 'ALL'
       END
      FROM pg_catalog.pg_policy AS pp
      JOIN pg_catalog.pg_class AS pc ON pc.oid = pp.polrelid
      JOIN pg_catalog.pg_namespace AS pn ON pn.oid = pc.relnamespace
     WHERE pc.relname = $1
       AND pp.polname = $2
       AND pn.nspname NOT IN ('pg_catalog', 'information_schema')
      INTO cmd;

    RETURN is( cmd, upper($3), $4 );
END;
$$ LANGUAGE plpgsql;

-- policy_cmd_is( table, policy, command )
CREATE OR REPLACE FUNCTION policy_cmd_is( NAME, NAME, text )
RETURNS TEXT AS $$
    SELECT policy_cmd_is(
        $1, $2, $3,
        'Policy ' || quote_ident($2)
        || ' for table ' || quote_ident($1)
        || ' should apply to ' || upper($3) || ' command'
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _funkargs ( NAME[] )
RETURNS TEXT AS $$
BEGIN
    RETURN array_to_string($1::regtype[], ',');
EXCEPTION WHEN undefined_object THEN
    RETURN array_to_string($1, ',');
END;
$$ LANGUAGE PLPGSQL STABLE;

CREATE OR REPLACE FUNCTION _got_func ( NAME, NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT TRUE
          FROM tap_funky
         WHERE schema = $1
           AND name   = $2
           AND args = _funkargs($3)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _got_func ( NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT TRUE
          FROM tap_funky
         WHERE name = $1
           AND args = _funkargs($2)
           AND is_visible
    );
$$ LANGUAGE SQL;

/*
 * Internal function to test whether the specified table in the specified schema
 * has an inheritance chain. Returns true or false.
 */
CREATE OR REPLACE FUNCTION _inherited( NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_namespace n
          JOIN pg_catalog.pg_class c ON n.oid = c.relnamespace
         WHERE c.relkind = 'r'
           AND n.nspname = $1
           AND c.relname = $2
           AND c.relhassubclass = true
  );
$$ LANGUAGE SQL;

/*
 * Internal function to test whether a specific table in the search_path has an
 * inheritance chain. Returns true or false.
 */
CREATE OR REPLACE FUNCTION _inherited( NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_class c
         WHERE c.relkind = 'r'
           AND pg_catalog.pg_table_is_visible( c.oid )
           AND c.relname = $1
           AND c.relhassubclass = true
    );
$$ LANGUAGE SQL;

-- has_inherited_tables( schema, table, description )
CREATE OR REPLACE FUNCTION has_inherited_tables( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _inherited( $1, $2 ), $3);
$$ LANGUAGE SQL;

-- has_inherited_tables( schema, table )
CREATE OR REPLACE FUNCTION has_inherited_tables( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _inherited( $1, $2 ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 ) || ' should have descendents'
    );
$$ LANGUAGE SQL;

-- has_inherited_tables( table, description )
CREATE OR REPLACE FUNCTION has_inherited_tables( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _inherited( $1 ), $2 );
$$ LANGUAGE SQL;

-- has_inherited_tables( table )
CREATE OR REPLACE FUNCTION has_inherited_tables( NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _inherited( $1 ),
        'Table ' || quote_ident( $1 ) || ' should have descendents'
    );
$$ LANGUAGE SQL;

-- hasnt_inherited_tables( schema, table, description )
CREATE OR REPLACE FUNCTION hasnt_inherited_tables( NAME, NAME, TEXT )
RETURNS TEXT AS $$
       SELECT ok( NOT _inherited( $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- hasnt_inherited_tables( schema, table )
CREATE OR REPLACE FUNCTION hasnt_inherited_tables( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _inherited( $1, $2 ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 ) || ' should not have descendents'
    );
$$ LANGUAGE SQL;

-- hasnt_inherited_tables( table, description )
CREATE OR REPLACE FUNCTION hasnt_inherited_tables( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _inherited( $1 ), $2 );
$$ LANGUAGE SQL;

-- hasnt_inherited_tables( table )
CREATE OR REPLACE FUNCTION hasnt_inherited_tables( NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _inherited( $1 ),
        'Table ' || quote_ident( $1 ) || ' should not have descendents'
    );
$$ LANGUAGE SQL;

/*
* Internal function to test whether the schema-qualified table is an ancestor of
* the other schema-qualified table. The integer value is the length of the
* inheritance chain: a direct ancestor has has a chain length of 1.
*/
CREATE OR REPLACE FUNCTION _ancestor_of( NAME, NAME, NAME, NAME, INT )
RETURNS BOOLEAN AS $$
    WITH RECURSIVE inheritance_chain AS (
        -- select the ancestor tuple
        SELECT i.inhrelid AS descendent_id, 1 AS inheritance_level
          FROM pg_catalog.pg_inherits i
        WHERE i.inhparent = (
            SELECT c1.oid
              FROM pg_catalog.pg_class c1
              JOIN pg_catalog.pg_namespace n1
                ON c1.relnamespace = n1.oid
             WHERE c1.relname = $2
               AND n1.nspname = $1
        )
        UNION
        -- select the descendents
        SELECT i.inhrelid AS descendent_id,
               p.inheritance_level + 1 AS inheritance_level
          FROM pg_catalog.pg_inherits i
          JOIN inheritance_chain p
            ON p.descendent_id = i.inhparent
         WHERE i.inhrelid = (
            SELECT c1.oid
              FROM pg_catalog.pg_class c1
              JOIN pg_catalog.pg_namespace n1
                ON c1.relnamespace = n1.oid
             WHERE c1.relname = $4
               AND n1.nspname = $3
        )
    )
    SELECT EXISTS(
        SELECT true
          FROM inheritance_chain
         WHERE inheritance_level = COALESCE($5, inheritance_level)
           AND descendent_id = (
                SELECT c1.oid
                  FROM pg_catalog.pg_class c1
                  JOIN pg_catalog.pg_namespace n1
                    ON c1.relnamespace = n1.oid
                 WHERE c1.relname = $4
                   AND n1.nspname = $3
        )
    );
$$ LANGUAGE SQL;

/*
 * Internal function to check if not-qualified tables
 * within the search_path are connected by an inheritance chain.
 */
CREATE OR REPLACE FUNCTION _ancestor_of( NAME, NAME, INT )
RETURNS BOOLEAN AS $$
    WITH RECURSIVE inheritance_chain AS (
        -- select the ancestor tuple
        SELECT i.inhrelid AS descendent_id, 1 AS inheritance_level
          FROM pg_catalog.pg_inherits i
        WHERE i.inhparent = (
            SELECT c1.oid
              FROM pg_catalog.pg_class c1
              JOIN pg_catalog.pg_namespace n1
                ON c1.relnamespace = n1.oid
             WHERE c1.relname = $1
               AND pg_catalog.pg_table_is_visible( c1.oid )
        )
        UNION
        -- select the descendents
        SELECT i.inhrelid AS descendent_id,
               p.inheritance_level + 1 AS inheritance_level
          FROM pg_catalog.pg_inherits i
          JOIN inheritance_chain p
            ON p.descendent_id = i.inhparent
         WHERE i.inhrelid = (
            SELECT c1.oid
              FROM pg_catalog.pg_class c1
              JOIN pg_catalog.pg_namespace n1
                ON c1.relnamespace = n1.oid
             WHERE c1.relname = $2
               AND pg_catalog.pg_table_is_visible( c1.oid )
        )
    )
    SELECT EXISTS(
        SELECT true
          FROM inheritance_chain
         WHERE inheritance_level = COALESCE($3, inheritance_level)
           AND descendent_id = (
                SELECT c1.oid
                  FROM pg_catalog.pg_class c1
                  JOIN pg_catalog.pg_namespace n1
                    ON c1.relnamespace = n1.oid
                 WHERE c1.relname = $2
                   AND pg_catalog.pg_table_is_visible( c1.oid )
        )
    );
$$ LANGUAGE SQL;

-- is_ancestor_of( schema, table, schema, table, depth, description )
CREATE OR REPLACE FUNCTION is_ancestor_of( NAME, NAME, NAME, NAME, INT, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ancestor_of( $1, $2, $3, $4, $5 ), $6 );
$$ LANGUAGE SQL;

-- is_ancestor_of( schema, table, schema, table, depth )
CREATE OR REPLACE FUNCTION is_ancestor_of( NAME, NAME, NAME, NAME, INT )
RETURNS TEXT AS $$
    SELECT ok(
        _ancestor_of( $1, $2, $3, $4, $5 ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 )
        || ' should be ancestor ' || $5 || ' for '
        || quote_ident( $3 ) || '.' || quote_ident( $4 )
    );
$$ LANGUAGE SQL;

-- is_ancestor_of( schema, table, schema, table, description )
CREATE OR REPLACE FUNCTION is_ancestor_of( NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ancestor_of( $1, $2, $3, $4, NULL ), $5 );
$$ LANGUAGE SQL;

-- is_ancestor_of( schema, table, schema, table )
CREATE OR REPLACE FUNCTION is_ancestor_of( NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _ancestor_of( $1, $2, $3, $4, NULL ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 )
        || ' should be an ancestor of '
        || quote_ident( $3 ) || '.' || quote_ident( $4 )
    );
$$ LANGUAGE SQL;

-- is_ancestor_of( table, table, depth, description )
CREATE OR REPLACE FUNCTION is_ancestor_of( NAME, NAME, INT, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ancestor_of( $1, $2, $3 ), $4 );
$$ LANGUAGE SQL;

-- is_ancestor_of( table, table, depth )
CREATE OR REPLACE FUNCTION is_ancestor_of( NAME, NAME, INT )
RETURNS TEXT AS $$
    SELECT ok(
        _ancestor_of( $1, $2, $3 ),
        'Table ' || quote_ident( $1 ) || ' should be ancestor ' || $3 || ' of ' || quote_ident( $2)
    );
$$ LANGUAGE SQL;

-- is_ancestor_of( table, table, description )
CREATE OR REPLACE FUNCTION is_ancestor_of( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ancestor_of( $1, $2, NULL ), $3 );
$$ LANGUAGE SQL;

-- is_ancestor_of( table, table )
CREATE OR REPLACE FUNCTION is_ancestor_of( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _ancestor_of( $1, $2, NULL ),
        'Table ' || quote_ident( $1 ) || ' should be an ancestor of ' || quote_ident( $2)
    );
$$ LANGUAGE SQL;

-- isnt_ancestor_of( schema, table, schema, table, depth, description )
CREATE OR REPLACE FUNCTION isnt_ancestor_of( NAME, NAME, NAME, NAME, INT, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT  _ancestor_of( $1, $2, $3, $4, $5 ), $6 );
$$ LANGUAGE SQL;

-- isnt_ancestor_of( schema, table, schema, table, depth )
CREATE OR REPLACE FUNCTION isnt_ancestor_of( NAME, NAME, NAME, NAME, INT )
RETURNS TEXT AS $$
    SELECT ok(
        NOT  _ancestor_of( $1, $2, $3, $4, $5 ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 )
        || ' should not be ancestor ' || $5 || ' for '
        || quote_ident( $3 ) || '.' || quote_ident( $4 )
    );
$$ LANGUAGE SQL;

-- isnt_ancestor_of( schema, table, schema, table, description )
CREATE OR REPLACE FUNCTION isnt_ancestor_of( NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT  _ancestor_of( $1, $2, $3, $4, NULL ), $5 );
$$ LANGUAGE SQL;

-- isnt_ancestor_of( schema, table, schema, table )
CREATE OR REPLACE FUNCTION isnt_ancestor_of( NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT  _ancestor_of( $1, $2, $3, $4, NULL ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 )
        || ' should not be an ancestor of '
        || quote_ident( $3 ) || '.' || quote_ident( $4 )
    );
$$ LANGUAGE SQL;

-- isnt_ancestor_of( table, table, depth, description )
CREATE OR REPLACE FUNCTION isnt_ancestor_of( NAME, NAME, INT, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT  _ancestor_of( $1, $2, $3 ), $4 );
$$ LANGUAGE SQL;

-- isnt_ancestor_of( table, table, depth )
CREATE OR REPLACE FUNCTION isnt_ancestor_of( NAME, NAME, INT )
RETURNS TEXT AS $$
    SELECT ok(
        NOT  _ancestor_of( $1, $2, $3 ),
        'Table ' || quote_ident( $1 ) || ' should not be ancestor ' || $3 || ' of ' || quote_ident( $2)
    );
$$ LANGUAGE SQL;

-- isnt_ancestor_of( table, table, description )
CREATE OR REPLACE FUNCTION isnt_ancestor_of( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT  _ancestor_of( $1, $2, NULL ), $3 );
$$ LANGUAGE SQL;

-- isnt_ancestor_of( table, table )
CREATE OR REPLACE FUNCTION isnt_ancestor_of( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT  _ancestor_of( $1, $2, NULL ),
        'Table ' || quote_ident( $1 ) || ' should not be an ancestor of ' || quote_ident( $2)
    );
$$ LANGUAGE SQL;

-- is_descendent_of( schema, table, schema, table, depth, description )
CREATE OR REPLACE FUNCTION is_descendent_of( NAME, NAME, NAME, NAME, INT, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ancestor_of( $3, $4, $1, $2, $5 ), $6 );
$$ LANGUAGE SQL;

-- is_descendent_of( schema, table, schema, table, depth )
CREATE OR REPLACE FUNCTION is_descendent_of( NAME, NAME, NAME, NAME, INT )
RETURNS TEXT AS $$
    SELECT ok(
        _ancestor_of( $3, $4, $1, $2, $5 ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 )
        || ' should be descendent ' || $5 || ' from '
        || quote_ident( $3 ) || '.' || quote_ident( $4 )
    );
$$ LANGUAGE SQL;

-- is_descendent_of( schema, table, schema, table, description )
CREATE OR REPLACE FUNCTION is_descendent_of( NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ancestor_of( $3, $4, $1, $2, NULL ), $5 );
$$ LANGUAGE SQL;

-- is_descendent_of( schema, table, schema, table )
CREATE OR REPLACE FUNCTION is_descendent_of( NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _ancestor_of( $3, $4, $1, $2, NULL ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 )
        || ' should be a descendent of '
        || quote_ident( $3 ) || '.' || quote_ident( $4 )
    );
$$ LANGUAGE SQL;

-- is_descendent_of( table, table, depth, description )
CREATE OR REPLACE FUNCTION is_descendent_of( NAME, NAME, INT, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ancestor_of( $2, $1, $3 ), $4 );
$$ LANGUAGE SQL;

-- is_descendent_of( table, table, depth )
CREATE OR REPLACE FUNCTION is_descendent_of( NAME, NAME, INT )
RETURNS TEXT AS $$
    SELECT ok(
        _ancestor_of( $2, $1, $3 ),
        'Table ' || quote_ident( $1 ) || ' should be descendent ' || $3 || ' from ' || quote_ident( $2)
    );
$$ LANGUAGE SQL;

-- is_descendent_of( table, table, description )
CREATE OR REPLACE FUNCTION is_descendent_of( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ancestor_of( $2, $1, NULL ), $3 );
$$ LANGUAGE SQL;

-- is_descendent_of( table, table )
CREATE OR REPLACE FUNCTION is_descendent_of( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _ancestor_of( $2, $1, NULL ),
        'Table ' || quote_ident( $1 ) || ' should be a descendent of ' || quote_ident( $2)
    );
$$ LANGUAGE SQL;

-- isnt_descendent_of( schema, table, schema, table, depth, description )
CREATE OR REPLACE FUNCTION isnt_descendent_of( NAME, NAME, NAME, NAME, INT, TEXT )
RETURNS TEXT AS $$
    SELECT ok(NOT  _ancestor_of( $3, $4, $1, $2, $5 ), $6 );
$$ LANGUAGE SQL;

-- isnt_descendent_of( schema, table, schema, table, depth )
CREATE OR REPLACE FUNCTION isnt_descendent_of( NAME, NAME, NAME, NAME, INT )
RETURNS TEXT AS $$
    SELECT ok(
       NOT  _ancestor_of( $3, $4, $1, $2, $5 ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 )
        || ' should not be descendent ' || $5 || ' from '
        || quote_ident( $3 ) || '.' || quote_ident( $4 )
    );
$$ LANGUAGE SQL;

-- isnt_descendent_of( schema, table, schema, table, description )
CREATE OR REPLACE FUNCTION isnt_descendent_of( NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok(NOT  _ancestor_of( $3, $4, $1, $2, NULL ), $5 );
$$ LANGUAGE SQL;

-- isnt_descendent_of( schema, table, schema, table )
CREATE OR REPLACE FUNCTION isnt_descendent_of( NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
       NOT  _ancestor_of( $3, $4, $1, $2, NULL ),
        'Table ' || quote_ident( $1 ) || '.' || quote_ident( $2 )
        || ' should not be a descendent of '
        || quote_ident( $3 ) || '.' || quote_ident( $4 )
    );
$$ LANGUAGE SQL;

-- isnt_descendent_of( table, table, depth, description )
CREATE OR REPLACE FUNCTION isnt_descendent_of( NAME, NAME, INT, TEXT )
RETURNS TEXT AS $$
    SELECT ok(NOT  _ancestor_of( $2, $1, $3 ), $4 );
$$ LANGUAGE SQL;

-- isnt_descendent_of( table, table, depth )
CREATE OR REPLACE FUNCTION isnt_descendent_of( NAME, NAME, INT )
RETURNS TEXT AS $$
    SELECT ok(
       NOT  _ancestor_of( $2, $1, $3 ),
        'Table ' || quote_ident( $1 ) || ' should not be descendent ' || $3 || ' from ' || quote_ident( $2)
    );
$$ LANGUAGE SQL;

-- isnt_descendent_of( table, table, description )
CREATE OR REPLACE FUNCTION isnt_descendent_of( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok(NOT  _ancestor_of( $2, $1, NULL ), $3 );
$$ LANGUAGE SQL;

-- isnt_descendent_of( table, table )
CREATE OR REPLACE FUNCTION isnt_descendent_of( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
       NOT  _ancestor_of( $2, $1, NULL ),
        'Table ' || quote_ident( $1 ) || ' should not be a descendent of ' || quote_ident( $2)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _def_is( TEXT, TEXT, anyelement, TEXT )
RETURNS TEXT AS $$
DECLARE
    thing text;
BEGIN
    -- Function or special SQL syntax.
    IF $1 ~ '^[^'']+[(]' OR $1 = ANY('{CURRENT_CATALOG,CURRENT_ROLE,CURRENT_SCHEMA,CURRENT_USER,SESSION_USER,USER}') THEN
        RETURN is( $1, $3, $4 );
    END IF;

    EXECUTE 'SELECT is('
             || COALESCE($1, 'NULL' || '::' || $2) || '::' || $2 || ', '
             || COALESCE(quote_literal($3), 'NULL') || '::' || $2 || ', '
             || COALESCE(quote_literal($4), 'NULL')
    || ')' INTO thing;
    RETURN thing;
END;
$$ LANGUAGE plpgsql;
