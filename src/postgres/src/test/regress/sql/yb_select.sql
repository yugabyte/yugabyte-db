--
-- SELECT
--

-- lsm index
-- awk '{if($1<10){print;}else{next;}}' onek.data | sort +0n -1
--
SELECT * FROM onek
   WHERE onek.unique1 < 10
   ORDER BY onek.unique1;

--
-- awk '{if($1<20){print $1,$14;}else{next;}}' onek.data | sort +0nr -1
--
SELECT onek.unique1, onek.stringu1 FROM onek
   WHERE onek.unique1 < 20
   ORDER BY unique1 using >;

--
-- awk '{if($1>980){print $1,$14;}else{next;}}' onek.data | sort +1d -2
--
SELECT onek.unique1, onek.stringu1 FROM onek
   WHERE onek.unique1 > 980
   ORDER BY stringu1 using <;

--
-- awk '{if($1>980){print $1,$16;}else{next;}}' onek.data |
-- sort +1d -2 +0nr -1
--
SELECT onek.unique1, onek.string4 FROM onek
   WHERE onek.unique1 > 980
   ORDER BY string4 using <, unique1 using >;

--
-- awk '{if($1>980){print $1,$16;}else{next;}}' onek.data |
-- sort +1dr -2 +0n -1
--
SELECT onek.unique1, onek.string4 FROM onek
   WHERE onek.unique1 > 980
   ORDER BY string4 using >, unique1 using <;

--
-- awk '{if($1<20){print $1,$16;}else{next;}}' onek.data |
-- sort +0nr -1 +1d -2
--
SELECT onek.unique1, onek.string4 FROM onek
   WHERE onek.unique1 < 20
   ORDER BY unique1 using >, string4 using <;

--
-- awk '{if($1<20){print $1,$16;}else{next;}}' onek.data |
-- sort +0n -1 +1dr -2
--
SELECT onek.unique1, onek.string4 FROM onek
   WHERE onek.unique1 < 20
   ORDER BY unique1 using <, string4 using >;

--
-- Test ORDER BY options
--

CREATE TEMP TABLE foo (f1 int);

INSERT INTO foo VALUES (42),(3),(10),(7),(null),(null),(1);

SELECT * FROM foo ORDER BY f1;
SELECT * FROM foo ORDER BY f1 ASC;	-- same thing
SELECT * FROM foo ORDER BY f1 NULLS FIRST;
SELECT * FROM foo ORDER BY f1 DESC;
SELECT * FROM foo ORDER BY f1 DESC NULLS LAST;

-- check if indexscans do the right things
CREATE INDEX fooi ON foo (f1);
SET enable_sort = false;

SELECT * FROM foo ORDER BY f1;
SELECT * FROM foo ORDER BY f1 NULLS FIRST;
SELECT * FROM foo ORDER BY f1 DESC;
SELECT * FROM foo ORDER BY f1 DESC NULLS LAST;

DROP INDEX fooi;
CREATE INDEX fooi ON foo (f1 DESC);

SELECT * FROM foo ORDER BY f1;
SELECT * FROM foo ORDER BY f1 NULLS FIRST;
SELECT * FROM foo ORDER BY f1 DESC;
SELECT * FROM foo ORDER BY f1 DESC NULLS LAST;

DROP INDEX fooi;
CREATE INDEX fooi ON foo (f1 DESC NULLS LAST);

SELECT * FROM foo ORDER BY f1;
SELECT * FROM foo ORDER BY f1 NULLS FIRST;
SELECT * FROM foo ORDER BY f1 DESC;
SELECT * FROM foo ORDER BY f1 DESC NULLS LAST;

-- TODO(jason): remove when issue #1721 is closed or closing.
DISCARD TEMP;

--
-- Test reverse scans with limit.
--
CREATE TABLE reverse_scan_test (
  h BIGINT,
  r INT,
  PRIMARY KEY(h, r ASC)
);

INSERT INTO reverse_scan_test
VALUES (1, 1),
       (1, 2),
       (1, 3),
       (1, 4),
       (1, 5),
       (1, 6),
       (1, 7),
       (1, 8),
       (1, 9),
       (1, 10),
       (1, 11),
       (1, 12),
       (1, 13),
       (1, 14);

-- Check ascending order.
SELECT r FROM reverse_scan_test WHERE h = 1;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 2 and r < 9;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 2 and r < 9 LIMIT 5;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 2 and r <= 13;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 2 and r <= 13 LIMIT 10;

-- Test desc order (reverse scan).
SELECT r FROM reverse_scan_test WHERE h = 1 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE h = 1 ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r < 9 ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 1 AND r < 14 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 1 AND r < 14 ORDER BY r DESC LIMIT 9;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 3 and r <= 13 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 3 and r <= 13 ORDER BY r DESC LIMIT 8;
