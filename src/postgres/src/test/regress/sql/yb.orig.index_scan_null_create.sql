-- Create tables for the null scan key tests
--
-- As of 2023-06-21, the tables will default to 3 tablets, but in case those
-- defaults change, explicitly set the numbers here.  The number of tablets
-- affects the number of requests shown in EXPLAIN DIST.

CREATE TABLE nulltest (a int, b int) SPLIT INTO 3 TABLETS;
INSERT INTO nulltest VALUES (null, null), (null, 1), (1, null), (1, 1);

CREATE TABLE nulltest2 (x int, y int) SPLIT INTO 3 TABLETS;
INSERT INTO nulltest2 VALUES (null, null);
