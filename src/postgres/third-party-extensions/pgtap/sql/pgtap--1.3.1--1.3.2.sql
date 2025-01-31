DROP FUNCTION parse_type(type text, OUT typid oid, OUT typmod int4);

CREATE OR REPLACE FUNCTION format_type_string ( TEXT )
RETURNS TEXT AS $$
DECLARE
    want_type TEXT := $1;
    typmodin_arg cstring[];
    typmodin_func regproc;
    typmod int;
BEGIN
    IF want_type::regtype = 'interval'::regtype THEN
        -- RAISE NOTICE 'cannot resolve: %', want_type;  -- TODO
        RETURN want_type;
    END IF;

    -- Extract type modifier from type declaration and format as cstring[] literal.
    typmodin_arg := translate(substring(want_type FROM '[(][^")]+[)]'), '()', '{}');

    -- Find typmodin function for want_type.
    SELECT typmodin INTO typmodin_func
      FROM pg_catalog.pg_type
     WHERE oid = want_type::regtype;

    IF typmodin_func = 0 THEN
        -- Easy: types without typemods.
        RETURN format_type(want_type::regtype, null);
    END IF;

    -- Get typemod via type-specific typmodin function.
    EXECUTE format('SELECT %I(%L)', typmodin_func, typmodin_arg) INTO typmod;
    RETURN format_type(want_type::regtype, typmod);
EXCEPTION WHEN OTHERS THEN RETURN NULL;
END;
$$ LANGUAGE PLPGSQL STABLE;
