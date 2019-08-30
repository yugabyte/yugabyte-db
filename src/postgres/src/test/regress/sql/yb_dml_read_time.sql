--
-- Read & write the same table in one statement.
--
CREATE TABLE read_time_test1(i INT, t TEXT);
INSERT INTO read_time_test1 VALUES(1, 'one');
INSERT INTO read_time_test1 SELECT * FROM read_time_test1;
INSERT INTO read_time_test1 SELECT * FROM read_time_test1;
INSERT INTO read_time_test1 SELECT * FROM read_time_test1;
INSERT INTO read_time_test1 SELECT * FROM read_time_test1;
INSERT INTO read_time_test1 SELECT * FROM read_time_test1;
INSERT INTO read_time_test1 SELECT * FROM read_time_test1;
SELECT count(*) FROM read_time_test1;
--
-- Write table and its UNIQUE index.
--
CREATE TABLE read_time_test2(i INT PRIMARY KEY, j INT UNIQUE, k INT);
INSERT INTO read_time_test2 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3), (4, 4, 4), (5, 5, 5),
                                   (6, 6, 6), (7, 7, 7), (8, 8, 8), (9, 9, 9);
UPDATE read_time_test2 SET k = 777;
SELECT * FROM read_time_test2 ORDER BY i;
--
-- DELETE and UPDATE the same table in one transaction.
--
CREATE TABLE read_time_test3(i INT PRIMARY KEY, j INT);
INSERT INTO read_time_test3 VALUES (1), (2), (3), (4);
BEGIN;
DELETE FROM read_time_test3 WHERE i > 0;
UPDATE read_time_test3 SET j = 10 WHERE i = 1;
END;
SELECT * FROM read_time_test3 ORDER BY i;
--
-- DELETE and INSERT the same table in one transaction.
-- 
CREATE TABLE read_time_test4(i INT PRIMARY KEY, j INT);
INSERT INTO read_time_test4 VALUES (1), (2), (3), (4);
BEGIN;
DELETE FROM read_time_test4;
INSERT INTO read_time_test4 VALUES (1), (2), (3), (4);
END;
SELECT * FROM read_time_test4 ORDER BY i;
--
-- Combine test cases into one transaction.
-- It appears DELETE has a bug, so the following test is disabled for now.
--

-- CREATE TABLE read_time_test(i INT, t TEXT, j INT UNIQUE, k INT, PRIMARY KEY(i, j));
-- BEGIN;
-- INSERT INTO read_time_test VALUES (1, 'one', 1), (2, 'two', 2), (3, 'three', 3), (4, 'four', 4);
-- DELETE FROM read_time_test WHERE i = 1;
-- INSERT INTO read_time_test VALUES (1, 'one', 1, 1);
-- INSERT INTO read_time_test VALUES (11, 'one', 11, 11);
-- SELECT * FROM read_time_test ORDER BY i;
-- DELETE FROM read_time_test WHERE i < 10;
-- INSERT INTO read_time_test VALUES (1, 'one', 1), (2, 'two', 2), (3, 'three', 3), (4, 'four', 4);
-- SELECT * FROM read_time_test ORDER BY i, t;
-- UPDATE read_time_test SET k = 20;
-- UPDATE read_time_test SET i = 20 WHERE i < 11;
-- UPDATE read_time_test SET i = 30 WHERE i = 11;
-- SELECT * FROM read_time_test ORDER BY i, t;
-- END;
-- SELECT * FROM read_time_test ORDER BY i, t;
