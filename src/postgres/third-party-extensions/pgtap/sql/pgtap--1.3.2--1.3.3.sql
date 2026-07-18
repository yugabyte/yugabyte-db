CREATE OR REPLACE FUNCTION format_type_string ( TEXT )
RETURNS TEXT AS $$
DECLARE
    want_type TEXT := $1;
BEGIN
    IF pg_version_num() >= 170000 THEN
        -- to_regtypemod() in 17 allows easy and corret normalization.
        RETURN format_type(to_regtype(want_type), to_regtypemod(want_type));
    END IF;

    IF want_type::regtype = 'interval'::regtype THEN
        -- We cannot normlize interval types without to_regtypemod(), So
        -- just return it as is.
        RETURN want_type;
    END IF;

    -- Use the typmodin functions to correctly normalize types.
    DECLARE
        typmodin_arg cstring[];
        typmodin_func regproc;
        typmod int;
    BEGIN
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
        EXECUTE format('SELECT %s(%L)', typmodin_func, typmodin_arg) INTO typmod;
        RETURN format_type(want_type::regtype, typmod);
    END;
    EXCEPTION WHEN OTHERS THEN RETURN NULL;
END;
$$ LANGUAGE PLPGSQL STABLE;
