CREATE OR REPLACE FUNCTION _get_func_privs(TEXT, TEXT)
RETURNS TEXT[] AS $$
BEGIN
    IF pg_catalog.has_function_privilege($1, $2, 'EXECUTE') THEN
        RETURN '{EXECUTE}';
    ELSE
        RETURN '{}';
    END IF;
EXCEPTION
    -- Not a valid func name.
    WHEN undefined_function THEN RETURN '{undefined_function}';
    -- Not a valid role.
    WHEN undefined_object   THEN RETURN '{undefined_role}';
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION _fprivs_are ( TEXT, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_func_privs($2, $1);
BEGIN
    IF grants[1] = 'undefined_function' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Function ' || $1 || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;
