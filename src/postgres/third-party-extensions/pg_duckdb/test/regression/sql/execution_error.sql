CREATE TABLE int_as_varchar(a varchar);
INSERT INTO int_as_varchar SELECT * from (
	VALUES
		('abc')
) t(a);

SELECT a::INTEGER FROM int_as_varchar;
DROP TABLE int_as_varchar;

\set VERBOSITY verbose
select * from duckdb.raw_query('aaaaa');
