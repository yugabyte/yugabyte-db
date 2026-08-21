CREATE USER duckdb_view_admin IN ROLE duckdb_group;
GRANT ALL ON SCHEMA public TO duckdb_view_admin;
CREATE USER duckdb_view_user1 IN ROLE duckdb_group;

SET ROLE duckdb_view_admin;

CREATE VIEW duckdb_view AS SELECT r['a']::int as a FROM duckdb.query($$ SELECT 1 a $$) r;
CREATE VIEW postgres_view AS SELECT 2 from generate_series(1,2);

SET ROLE duckdb_view_user1;

SELECT * from duckdb_view;
SELECT * from postgres_view;
SELECT * from duckdb.query($$ FROM pgduckdb.public.duckdb_view $$);
SELECT * from duckdb.query($$ FROM pgduckdb.public.postgres_view $$);

SET ROLE duckdb_view_admin;
GRANT SELECT ON duckdb_view TO duckdb_view_user1;
GRANT SELECT ON postgres_view TO duckdb_view_user1;
SET ROLE duckdb_view_user1;

SELECT * from duckdb_view;
SELECT * from postgres_view;
SELECT * from duckdb.query($$ FROM pgduckdb.public.duckdb_view $$);
SELECT * from duckdb.query($$ FROM pgduckdb.public.postgres_view $$);
