--
-- TIME
--

CREATE TABLE TIME_TBL (f1 time(2));

INSERT INTO TIME_TBL VALUES ('00:00');
INSERT INTO TIME_TBL VALUES ('01:00');
-- as of 7.4, timezone spec should be accepted and ignored
INSERT INTO TIME_TBL VALUES ('02:03 PST');
INSERT INTO TIME_TBL VALUES ('11:59 EDT');
INSERT INTO TIME_TBL VALUES ('12:00');
INSERT INTO TIME_TBL VALUES ('12:01');
INSERT INTO TIME_TBL VALUES ('23:59');
INSERT INTO TIME_TBL VALUES ('11:59:59.99 PM');
INSERT INTO TIME_TBL VALUES ('24:00');
INSERT INTO TIME_TBL VALUES ('23:59:59.999');

INSERT INTO TIME_TBL VALUES ('2003-03-07 15:36:39 America/New_York');
INSERT INTO TIME_TBL VALUES ('2003-07-07 15:36:39 America/New_York');
-- this should fail (the timezone offset is not known)
INSERT INTO TIME_TBL VALUES ('15:36:39 America/New_York');

-- Out of range values
INSERT INTO TIME_TBL VALUES ('25:00');
INSERT INTO TIME_TBL VALUES ('24:01');

SELECT f1 AS "Time" FROM TIME_TBL ORDER BY f1;

SELECT f1 AS "Three" FROM TIME_TBL WHERE f1 < '05:06:07' ORDER BY f1;

SELECT f1 AS "Five" FROM TIME_TBL WHERE f1 > '05:06:07' ORDER BY f1;

SELECT f1 AS "None" FROM TIME_TBL WHERE f1 < '00:00' ORDER BY f1;

SELECT f1 AS "Eight" FROM TIME_TBL WHERE f1 >= '00:00' ORDER BY f1;

-- ASC/DESC check
SELECT * FROM TIME_TBL ORDER BY f1 ASC;
SELECT * FROM TIME_TBL ORDER BY f1 DESC;

--
-- TIME simple math
--
-- We now make a distinction between time and intervals,
-- and adding two times together makes no sense at all.
-- Leave in one query to show that it is rejected,
-- and do the rest of the testing in horology.sql
-- where we do mixed-type arithmetic. - thomas 2000-12-02

SELECT f1 + time '00:01' AS "Illegal" FROM TIME_TBL;

--
-- Test for PRIMARY KEY
--

CREATE TABLE time_data_with_pk(id TIME PRIMARY KEY, val time);
INSERT INTO time_data_with_pk VALUES ('01:01:01','01:11:21');
INSERT INTO time_data_with_pk VALUES ('02:02:02','02:12:22');
INSERT INTO time_data_with_pk VALUES ('03:03:03','03:13:23');
SELECT * FROM time_data_with_pk ORDER BY id;
SELECT VAL FROM time_data_with_pk WHERE id = '02:02:02';
