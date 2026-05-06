create temp table users (data jsonb);
insert into users values ('{"a": 123}');

create or replace function get_users() returns setof users as
$$ SELECT * FROM users; $$
language sql stable;

SET duckdb.force_execution = true;
-- This command used to crash due to DuckDB returning data in the json
-- format instead of in jsonb format.
SELECT * FROM ROWS FROM(get_users()) WITH ORDINALITY;
