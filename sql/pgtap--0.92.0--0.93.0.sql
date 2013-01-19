-- _get_schema_owner( schema )
CREATE OR REPLACE FUNCTION _get_schema_owner( NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(nspowner)
      FROM pg_catalog.pg_namespace
     WHERE nspname = $1;
$$ LANGUAGE SQL;

-- schema_owner_is ( schema, user, description )
CREATE OR REPLACE FUNCTION schema_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_schema_owner($1);
BEGIN
    -- Make sure the schema exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Schema ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- schema_owner_is ( schema, user )
CREATE OR REPLACE FUNCTION schema_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT schema_owner_is(
        $1, $2,
        'Schema ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;
