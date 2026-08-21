-- Tests normalization (masking) of constants in QPM plan text.

drop schema if exists qpm_plan_normalization cascade;
create schema qpm_plan_normalization;
reset all;
set search_path to qpm_plan_normalization;

set yb_pg_stat_plans_track=top;
set yb_pg_stat_plans_track_catalog_queries=false;
set yb_pg_stat_plans_verbose_plans=true;

CREATE TABLE simple_table1(c1 INT, c2 INT, c3 text);
CREATE INDEX simple_index1 ON simple_table1(c2);
INSERT INTO simple_table1 VALUES(1, 1, 'abc');

/*+ SeqScan(st) */ /* QPM NORMALIZE QUERY 1 */ SELECT c1+5,
						 99,
						 'xyzw',
						 c3 || 'xyz',
						 U&'!0041!0042' UESCAPE '!',
						 X'1F'::INT + c2,
						 TIMESTAMP '2026-04-20 14:30:00',
						 true,
						 null,
						 CASE WHEN c1+1<5 THEN TRUE ELSE FALSE END,
						 SQRT(c2+1),
						 COALESCE(NULL, NULL, '0'::INT/4, 10/c1),
						 '\x48656c6c6f0a'::bytea,
						 CAST('123' AS numeric),
						 CAST(3.14159 AS numeric),
						 CAST(true::int AS numeric),
						 '99.95'::numeric,
						 CAST('123.456' AS numeric(6,2)),
						 CAST('1.23e4' AS numeric),
						 CAST('9.876e2' AS numeric(10,2))
FROM simple_table1 st WHERE c2<9+1;

SELECT plan FROM yb_pg_stat_plans sp JOIN pg_stat_statements ss ON sp.queryid=ss.queryid
WHERE query LIKE '%QPM NORMALIZE QUERY 1%';

/*+ HashJoin(st1 st2) Leading((st1 st2)) IndexScan(st1) SeqScan(st2) */
/* QPM NORMALIZE QUERY 2 */
SELECT SUM(st1.c1+5)
    FROM simple_table1 st1 LEFT JOIN (SELECT 1 c1, c2%9 c2, null c3 FROM simple_table1) st2
								ON st1.c1=st2.c1+1
	WHERE st1.c2<7 AND (st2.c2<>8 OR st2.c2 IS NULL);

SELECT plan FROM yb_pg_stat_plans sp LEFT JOIN pg_stat_statements ss ON sp.queryid=ss.queryid
WHERE query LIKE '%QPM NORMALIZE QUERY 2%';

SELECT /* QPM NORMALIZE QUERY 3 */
E'This is a string with:\n\
- a newline\n\
- a tab\tbetween words\n\
- a backslash \\\n\
- a single quote \' inside\n\
- unicode snowman: \u2603'
FROM simple_table1;

SELECT plan FROM yb_pg_stat_plans sp JOIN pg_stat_statements ss ON sp.queryid=ss.queryid
WHERE query LIKE '%QPM NORMALIZE QUERY 3%';

SELECT /* QPM NORMALIZE QUERY 4 */
	   'It''s a test string with a ''quoted'' word',
       '{"path": "C:\\Program Files\\App", "note": "Line1\nLine2"}'::jsonb,
	   E'{"text": "Hello\\nWorld", "quote": "O\'Reilly"}'::jsonb,
	   convert_to('Hello\nWorld', 'UTF8'),
	   '',
	   ''''
FROM simple_table1;

SELECT plan FROM yb_pg_stat_plans sp JOIN pg_stat_statements ss ON sp.queryid=ss.queryid
WHERE query LIKE '%QPM NORMALIZE QUERY 4%';

SELECT /* QPM NORMALIZE QUERY 5 */
       c1 FROM simple_table1 ORDER BY c2 + 50000;

SELECT plan FROM yb_pg_stat_plans sp JOIN pg_stat_statements ss ON sp.queryid=ss.queryid
WHERE query LIKE '%QPM NORMALIZE QUERY 5%';

SELECT /* QPM NORMALIZE QUERY 6 */
       c1 + 50000, count(*) FROM simple_table1 GROUP BY c1 + 50000 ORDER BY c1 + 50000;

SELECT plan FROM yb_pg_stat_plans sp JOIN pg_stat_statements ss ON sp.queryid=ss.queryid
WHERE query LIKE '%QPM NORMALIZE QUERY 6%';

SELECT /* QPM NORMALIZE QUERY 7 */
       'x' || 'y', sum(1+2+CAST('9.876e2' AS numeric(10,2))) FROM simple_table1 GROUP BY 'x' || 'y';

SELECT plan FROM yb_pg_stat_plans sp JOIN pg_stat_statements ss ON sp.queryid=ss.queryid
WHERE query LIKE '%QPM NORMALIZE QUERY 7%';

-- Negative tests.

CREATE TABLE /* QPM NORMALIZE QUERY 8 */ test_copy AS SELECT * FROM simple_table1 WHERE c1 > 0;

SELECT query, plan FROM yb_pg_stat_plans sp RIGHT JOIN pg_stat_statements ss ON sp.queryid=ss.queryid
WHERE query LIKE '%QPM NORMALIZE QUERY 8%';

EXPLAIN /* QPM NORMALIZE QUERY 9 */ SELECT * FROM simple_table1 WHERE c1 > 0;

SELECT query, plan FROM yb_pg_stat_plans sp RIGHT JOIN pg_stat_statements ss ON sp.queryid=ss.queryid
WHERE query LIKE '%QPM NORMALIZE QUERY 9%';

drop schema qpm_plan_normalization cascade;
