CREATE EXTENSION pg_stat_monitor;
SELECT pg_stat_monitor_reset();
CREATE TABLE foo1(a int);
CREATE TABLE foo2(b int);
CREATE TABLE foo3(c int);
CREATE TABLE foo4(d int);

-- test the simple table names
SELECT pg_stat_monitor_reset();
SELECT * FROM foo1;
SELECT * FROM foo1, foo2;
SELECT * FROM foo1, foo2, foo3;
SELECT * FROM foo1, foo2, foo3, foo4;
SELECT query, relations from pg_stat_monitor ORDER BY query;
SELECT pg_stat_monitor_reset();


-- test the schema qualified table
CREATE schema sch1;
CREATE schema sch2;
CREATE schema sch3;
CREATE schema sch4;

CREATE TABLE sch1.foo1(a int);
CREATE TABLE sch2.foo2(b int);
CREATE TABLE sch3.foo3(c int);
CREATE TABLE sch4.foo4(d int);

SELECT pg_stat_monitor_reset();
SELECT * FROM sch1.foo1;
SELECT * FROM sch1.foo1, sch2.foo2;
SELECT * FROM sch1.foo1, sch2.foo2, sch3.foo3;
SELECT * FROM sch1.foo1, sch2.foo2, sch3.foo3, sch4.foo4;
SELECT query, relations from pg_stat_monitor ORDER BY query;
SELECT pg_stat_monitor_reset();

SELECT pg_stat_monitor_reset();
SELECT * FROM sch1.foo1, foo1;
SELECT * FROM sch1.foo1, sch2.foo2, foo1, foo2;
SELECT query, relations from pg_stat_monitor ORDER BY query;
SELECT pg_stat_monitor_reset();

-- test the view
CREATE VIEW v1 AS SELECT * from foo1;
CREATE VIEW v2 AS SELECT * from foo1,foo2;
CREATE VIEW v3 AS SELECT * from foo1,foo2,foo3;
CREATE VIEW v4 AS SELECT * from foo1,foo2,foo3,foo4;

SELECT pg_stat_monitor_reset();
SELECT * FROM v1;
SELECT * FROM v1,v2;
SELECT * FROM v1,v2,v3;
SELECT * FROM v1,v2,v3,v4;
SELECT query, relations from pg_stat_monitor ORDER BY query;
SELECT pg_stat_monitor_reset();


DROP VIEW v1;
DROP VIEW v2;
DROP VIEW v3;
DROP VIEW v4;

DROP TABLE foo1;
DROP TABLE foo2;
DROP TABLE foo3;
DROP TABLE foo4;

DROP TABLE sch1.foo1;
DROP TABLE sch2.foo2;
DROP TABLE sch3.foo3;
DROP TABLE sch4.foo4;

DROP SCHEMA sch1;
DROP SCHEMA sch2;
DROP SCHEMA sch3;
DROP SCHEMA sch4;


DROP EXTENSION pg_stat_monitor;
