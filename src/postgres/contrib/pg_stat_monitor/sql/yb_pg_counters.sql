CREATE EXTENSION pg_stat_monitor;
SELECT pg_stat_monitor_reset();

CREATE TABLE t1 (a INTEGER);
CREATE TABLE t2 (b INTEGER);
CREATE TABLE t3 (c INTEGER);
CREATE TABLE t4 (d INTEGER);

SELECT pg_stat_monitor_reset();
SELECT a,b,c,d FROM t1, t2, t3, t4 WHERE t1.a = t2.b AND t3.c = t4.d ORDER BY a;
SELECT a,b,c,d FROM t1, t2, t3, t4 WHERE t1.a = t2.b AND t3.c = t4.d ORDER BY a;
SELECT a,b,c,d FROM t1, t2, t3, t4 WHERE t1.a = t2.b AND t3.c = t4.d ORDER BY a;
SELECT a,b,c,d FROM t1, t2, t3, t4 WHERE t1.a = t2.b AND t3.c = t4.d ORDER BY a;
SELECT query,calls FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();

SELECT pg_stat_monitor_reset();
do $$
declare
   n integer:= 1;
begin
	loop
		PERFORM a,b,c,d FROM t1, t2, t3, t4 WHERE t1.a = t2.b AND t3.c = t4.d ORDER BY a;
		exit when n = 1000;
		n := n + 1;
	end loop;
end $$;
SELECT query,calls FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();

DROP TABLE t1;
DROP TABLE t2;
DROP TABLE t3;
DROP TABLE t4;

DROP EXTENSION pg_stat_monitor;
