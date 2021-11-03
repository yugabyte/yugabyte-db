--
-- SELECT
--

CREATE TABLE nr (i int, j int, PRIMARY KEY (j ASC));
CREATE INDEX ON nr (i ASC);
INSERT INTO nr VALUES (1, 2), (3, 4);
BEGIN;
INSERT INTO nr VALUES (null, 6);
SELECT i FROM nr ORDER BY i;
SELECT i FROM nr ORDER BY i NULLS FIRST;
SELECT i FROM nr ORDER BY i DESC;
SELECT i FROM nr ORDER BY i DESC NULLS LAST;
ROLLBACK;

CREATE TABLE nr2 (i int, j int, PRIMARY KEY (j ASC));
CREATE INDEX ON nr2 (i DESC);
INSERT INTO nr2 VALUES (1, 2), (3, 4);
BEGIN;
INSERT INTO nr2 VALUES (null, 6);
SELECT i FROM nr2 ORDER BY i;
SELECT i FROM nr2 ORDER BY i NULLS FIRST;
SELECT i FROM nr2 ORDER BY i DESC;
SELECT i FROM nr2 ORDER BY i DESC NULLS LAST;
ROLLBACK;

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

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint);

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 2 and r < 9;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r > 2 and r < 9;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 2 and r < 9 LIMIT 5;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r > 2 and r < 9 LIMIT 5;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 2 and r <= 13;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r >= 2 and r <= 13;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 2 and r <= 13 LIMIT 10;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r >= 2 and r <= 13 LIMIT 10;

-- Test desc order (reverse scan).
SELECT r FROM reverse_scan_test WHERE h = 1 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE h = 1 ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r < 9 ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r < 9 ORDER BY r DESC LIMIT 5;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 1 AND r < 14 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r > 1 AND r < 14 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r > 1 AND r < 14 ORDER BY r DESC LIMIT 9;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r > 1 AND r < 14 ORDER BY r DESC LIMIT 9;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 3 and r <= 13 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r >= 3 and r <= 13 ORDER BY r DESC;

SELECT r FROM reverse_scan_test WHERE h = 1 AND r >= 3 and r <= 13 ORDER BY r DESC LIMIT 8;

SELECT r FROM reverse_scan_test WHERE yb_hash_code(h) = yb_hash_code(1::bigint) AND r >= 3 and r <= 13 ORDER BY r DESC LIMIT 8;
