DO $$
BEGIN
    IF pg_version_num() < 9.0 THEN
        EXECUTE $E$
            CREATE FUNCTION pg_tablespace_location(
                OID
            ) RETURNS TEXT LANGUAGE SQL AS $F$
                SELECT spclocation
                  FROM pg_catalog.pg_tablespace
                 WHERE OID = $1;
            $F$;
        $E$;
    END IF;
END;
$$;

CREATE OR REPLACE FUNCTION has_tablespace( NAME, TEXT, TEXT )
RETURNS TEXT AS $$
    SELECT ok(
        EXISTS(
            SELECT true
              FROM pg_catalog.pg_tablespace
             WHERE spcname = $1
               AND pg_tablespace_location(oid) = $2
        ), $3
    );
$$ LANGUAGE sql;
