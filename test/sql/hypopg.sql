-- SETUP
CREATE EXTENSION hypopg;

CREATE TABLE hypo (id integer, val text);

INSERT INTO hypo SELECT i, 'line ' || i
FROM generate_series(1,100000) f(i);

ANALYZE hypo;

-- TESTS
SELECT COUNT(*) AS nb
FROM public.hypopg_create_index('SELECT 1;CREATE INDEX ON hypo(id); SELECT 2');

SELECT nspname, relname, amname FROM public.hypopg_list_indexes();
