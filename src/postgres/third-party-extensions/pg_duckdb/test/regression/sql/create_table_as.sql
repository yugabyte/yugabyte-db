\set pwd `pwd`
CREATE TABLE webpages AS SELECT r['column00'], r['column01'], r['column02'] FROM read_csv(:'pwd' || '/data/web_page.csv') r;

select * from webpages order by column00 limit 2;
select count(*) from webpages;
SELECT attname, atttypid::regtype FROM pg_attribute WHERE attrelid = 'webpages'::regclass AND attname in ('column00', 'column01', 'column02') ORDER BY attname;

CREATE TEMP TABLE t_jsonb(data jsonb);
INSERT INTO t_jsonb VALUES ('{"a": 1, "b": 2}');
CREATE TEMP TABLE t_json AS SELECT * FROM t_jsonb WITH NO DATA;
SELECT * FROM t_json;
-- DuckDB table should have
SELECT atttypid::regtype FROM pg_attribute WHERE attrelid = 't_json'::regclass AND attname = 'data';

DROP TABLE webpages;
