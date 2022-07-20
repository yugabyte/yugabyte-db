-- Create tables for the null scan key tests

CREATE TABLE nulltest (a int, b int);
INSERT INTO nulltest VALUES (null, null), (null, 1), (1, null), (1, 1);

CREATE TABLE nulltest2 (x int, y int);
INSERT INTO nulltest2 VALUES (null, null);
