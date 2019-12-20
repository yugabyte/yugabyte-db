--
-- Colocation
--

CREATE DATABASE colocation_test colocated = true;
\c colocation_test

-- TODO: This test should be changed once we complete issue #3034
CREATE TABLE foo1 (a INT); -- fail
CREATE TABLE foo1 (a INT, PRIMARY KEY (a ASC));
CREATE TABLE foo2 (a INT, b INT, PRIMARY KEY (a ASC));
-- opt out of using colocated tablet
CREATE TABLE foo3 (a INT) WITH (colocated = false);
-- multi column primary key table
CREATE TABLE foo4(a INT, b INT, PRIMARY KEY (a ASC, b DESC));
CREATE TABLE foo5 (a INT, PRIMARY KEY (a ASC)) WITH (colocated = true);

INSERT INTO foo1 (a) VALUES (0), (1), (2);
INSERT INTO foo1 (a, b) VALUES (0, '0'); -- fail
INSERT INTO foo2 (a, b) VALUES (0, '0'), (1, '1');
INSERT INTO foo3 (a) VALUES (0), (1), (2), (3);
INSERT INTO foo4 (a, b) VALUES (0, 0), (0, 1), (1, 0), (1, 1);
INSERT INTO foo5 (a) VALUES (0), (1), (2), (3);

SELECT * FROM foo1;
SELECT * FROM foo1 WHERE a = 2;
SELECT * FROM foo1 WHERE n = '0'; -- fail
SELECT * FROM foo2;
SELECT * FROM foo3 ORDER BY a ASC;
SELECT * FROM foo4;
SELECT * FROM foo5;

-- table with index
CREATE TABLE bar1 (a INT, b INT, PRIMARY KEY (a ASC));
CREATE INDEX ON bar1 (a);
INSERT INTO bar1 (a, b) VALUES (0, 0), (1, 1), (2, 2), (3, 3), (4, 4), (5, 5);
EXPLAIN SELECT * FROM bar1 WHERE a = 1;
SELECT * FROM bar1 WHERE a = 1;
UPDATE bar1 SET b = b + 1 WHERE a > 3;
SELECT * FROM bar1;
DELETE FROM bar1 WHERE a > 3;
SELECT * FROM bar1;

-- colocated table with non-colocated index
CREATE TABLE bar2 (a INT, b INT, PRIMARY KEY (a ASC));
CREATE INDEX ON bar2 (a) WITH (colocated = true);
INSERT INTO bar2 (a, b) VALUES (0, 0), (1, 1), (2, 2), (3, 3), (4, 4), (5, 5);
EXPLAIN SELECT * FROM bar2 WHERE a = 1;
SELECT * FROM bar2 WHERE a = 1;
UPDATE bar2 SET b = b + 1 WHERE a > 3;
SELECT * FROM bar2;
DELETE FROM bar2 WHERE a > 3;
SELECT * FROM bar2;

-- colocated table with colocated index
CREATE TABLE bar3 (a INT, b INT, PRIMARY KEY (a ASC));
CREATE INDEX ON bar3 (a) WITH (colocated = false);
INSERT INTO bar3 (a, b) VALUES (0, 0), (1, 1), (2, 2), (3, 3), (4, 4), (5, 5);
EXPLAIN SELECT * FROM bar3 WHERE a = 1;
SELECT * FROM bar3 WHERE a = 1;
UPDATE bar3 SET b = b + 1 WHERE a > 3;
SELECT * FROM bar3;
DELETE FROM bar3 WHERE a > 3;
SELECT * FROM bar3;
