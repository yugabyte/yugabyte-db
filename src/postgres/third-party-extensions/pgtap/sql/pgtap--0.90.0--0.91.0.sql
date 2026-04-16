CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.91;'
LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION has_tablespace( NAME, TEXT, TEXT )
RETURNS TEXT AS $$
BEGIN
    IF pg_version_num() >= 90200 THEN
        RETURN ok(
            EXISTS(
                SELECT true
                  FROM pg_catalog.pg_tablespace
                 WHERE spcname = $1
                   AND pg_tablespace_location(oid) = $2
            ), $3
        );
    ELSE
        RETURN ok(
            EXISTS(
                SELECT true
                  FROM pg_catalog.pg_tablespace
                 WHERE spcname = $1
                   AND spclocation = $2
            ), $3
        );
    END IF;
END;
$$ LANGUAGE plpgsql;

