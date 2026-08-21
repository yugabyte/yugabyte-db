-- Setup
select duckdb.raw_query('CREATE TABLE union_tbl1 (u UNION(num INTEGER, str VARCHAR));');
select duckdb.raw_query('INSERT INTO union_tbl1 VALUES (1), (''two''), (union_value(str := ''three''));');

select * from duckdb.query($$ Select * from union_tbl1 $$);

select union_tag(r['u']) from duckdb.query($$ Select u from union_tbl1 $$) r;

select union_extract(r['u'],'str') from duckdb.query($$ Select u from union_tbl1 $$) r;
select union_extract(r['u'],'num') from duckdb.query($$ Select * from union_tbl1 $$) r;

SELECT union_extract(r['u'], 'str') FROM duckdb.query($$ Select u from union_tbl1 $$) r WHERE union_tag(r['u']) = 'str';
SELECT union_extract(r['u'], 'str') FROM duckdb.query($$ Select u from union_tbl1 $$) r WHERE union_extract(r['u'], 'str') IS NOT NULL;

select duckdb.raw_query('DROP TABLE union_tbl1;');
