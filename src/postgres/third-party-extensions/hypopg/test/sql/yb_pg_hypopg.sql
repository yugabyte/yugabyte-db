-- SETUP
CREATE OR REPLACE FUNCTION do_explain(stmt text) RETURNS table(a text) AS
$_$
DECLARE
    ret text;
BEGIN
    FOR ret IN EXECUTE format('EXPLAIN (FORMAT text) %s', stmt) LOOP
        a := ret;
        RETURN next ;
    END LOOP;
END;
$_$
LANGUAGE plpgsql;

CREATE EXTENSION hypopg;

CREATE TABLE hypo (id integer, val text);

INSERT INTO hypo SELECT i, 'line ' || i
FROM generate_series(1,100000) f(i);
