---
--- YB Index Recheck Test
---
--- Cases:
--- 1. Conditions that are trying to bind to a column that already has a higher
---    priority condition bound to it.  We prioritize equality filters over
---    inequality filters.
--- 2. Incomplete bindings to hash keys.
--- 3. Conditions whose operands don’t match in type. (including UDTs)
--- 4. Unsupported RowComparisonExpressions

\set explain_analyze 'EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF)'
SET enable_seqscan = off;

--- - ---
--- 1 --- Columns with a higher priority condition
--- - ---

CREATE TABLE t_multiple_binds(a int, b int, primary key(a asc, b asc));
CREATE INDEX t_a_b_idx ON t_multiple_binds(a ASC, b ASC);
INSERT INTO t_multiple_binds SELECT i, j FROM generate_series(1, 10) i, generate_series(1, 10) j;
-- These two can send all conditions because the conditions are on seperate keys. No recheck required.
:explain_analyze /*+ IndexScan(t t_multiple_binds_pkey) */ SELECT * FROM t_multiple_binds t WHERE a IN (1,2) and b <= 5;
:explain_analyze /*+ IndexScan(t t_a_b_idx) */ SELECT * FROM t_multiple_binds t WHERE a IN (1,2) and b <= 5;
-- These can only bind one condition to the key, so recheck is required.
:explain_analyze /*+ IndexScan(t t_multiple_binds_pkey) */ SELECT * FROM t_multiple_binds t WHERE a <= 7 AND a = 8;
:explain_analyze /*+ IndexScan(t t_multiple_binds_pkey) */ SELECT * FROM t_multiple_binds t WHERE b <= 7 AND b = 8;
:explain_analyze /*+ IndexScan(t t_a_b_idx) */ SELECT * FROM t_multiple_binds t WHERE a <= 7 AND a = 8;
:explain_analyze /*+ IndexScan(t t_a_b_idx) */ SELECT * FROM t_multiple_binds t WHERE b <= 7 AND b = 8;

DROP TABLE t_multiple_binds;

--- - ---
--- 2 --- Incomplete bindings to hash partition keys (#26742)
--- - ---

CREATE TABLE t_incomplete_binds (h1 int, h2 int, r1 int, r2 int);
INSERT INTO t_incomplete_binds SELECT i, j, i, j FROM generate_series(1, 10) i, generate_series(1, 10) j;
CREATE INDEX t_hash_hash ON t_incomplete_binds(h1, h2);
CREATE INDEX t_hash_range ON t_incomplete_binds(h1, r2 ASC);
CREATE INDEX t_range_range ON t_incomplete_binds(r1 ASC, r2 ASC);

-- full bindings (no recheck required)
:explain_analyze SELECT * FROM t_incomplete_binds WHERE h1 = 1 AND h2 = 5;
:explain_analyze SELECT * FROM t_incomplete_binds WHERE h1 = 1 AND r2 < 5;
:explain_analyze SELECT * FROM t_incomplete_binds WHERE r1 < 1 AND r2 < 5;

-- partial bindings (recheck required)
:explain_analyze /*+ IndexScan(t t_hash_hash) */ SELECT * FROM t_incomplete_binds t WHERE h2 = 5;
:explain_analyze /*+ IndexScan(t t_hash_range) */ SELECT * FROM t_incomplete_binds t WHERE r2 < 5;
:explain_analyze /*+ IndexScan(t t_range_range) */ SELECT * FROM t_incomplete_binds t WHERE r2 < 5;

DROP TABLE t_incomplete_binds;

--- - ---
--- 3 --- Cross type index comparisons
--- - ---

----- integer comparisons ----
CREATE TABLE t_int(i2 int2, i4 int4, i8 int8, f4 float4, f8 float8);
CREATE INDEX t_int_i2 ON t_int(i2 ASC);
CREATE INDEX t_int_i4 ON t_int(i4 ASC);
CREATE INDEX t_int_i8 ON t_int(i8 ASC);
INSERT INTO t_int VALUES (     1,           1,                    1);
INSERT INTO t_int VALUES (     2,           2,                    3);
INSERT INTO t_int VALUES (-32768, -2147483648, -9223372036854775808);
INSERT INTO t_int VALUES ( 32767,  2147483647,  9223372036854775807);

-- these are each within range, so no recheck is required
:explain_analyze SELECT * FROM t_int WHERE i2 = 1;
:explain_analyze SELECT * FROM t_int WHERE i2 > 1;
:explain_analyze SELECT * FROM t_int WHERE i2 < 1;
-- these are each out of range, so recheck is required
:explain_analyze SELECT * FROM t_int WHERE i2 = 32768;
:explain_analyze SELECT * FROM t_int WHERE i2 < 32768;
:explain_analyze SELECT * FROM t_int WHERE i2 > 32768;
:explain_analyze SELECT * FROM t_int WHERE i2 IN (1, 2, 32767, 32768);

-- these are each within range, so no recheck is required
:explain_analyze SELECT * FROM t_int WHERE i4 = 1;
:explain_analyze SELECT * FROM t_int WHERE i4 > 1;
:explain_analyze SELECT * FROM t_int WHERE i4 < 1;
-- these are each out of range, so recheck is required
:explain_analyze SELECT * FROM t_int WHERE i4 = 2147483648;
:explain_analyze SELECT * FROM t_int WHERE i4 > 2147483648;
:explain_analyze SELECT * FROM t_int WHERE i4 < 2147483648;
:explain_analyze SELECT * FROM t_int WHERE i4 IN (1, 2, 2147483647, 2147483648);

-- these are each within range, so no recheck is required
:explain_analyze SELECT * FROM t_int WHERE i8 = 1;
:explain_analyze SELECT * FROM t_int WHERE i8 > 1;
:explain_analyze SELECT * FROM t_int WHERE i8 < 1;
-- these are each out of range, so recheck is required
:explain_analyze SELECT * FROM t_int WHERE i8 = 9223372036854775809;
:explain_analyze SELECT * FROM t_int WHERE i8 > 9223372036854775809;
:explain_analyze SELECT * FROM t_int WHERE i8 < 9223372036854775809;
:explain_analyze SELECT * FROM t_int WHERE i8 IN (1, 2, 9223372036854775807, 9223372036854775808);

DROP TABLE t_int;

----- float comparisons ----
CREATE TABLE t_float(f4 float4, f8 float8);
CREATE INDEX t_float_f4 ON t_float(f4 ASC);
CREATE INDEX t_float_f8 ON t_float(f8 ASC);

INSERT INTO t_float VALUES (1,                    1);
INSERT INTO t_float VALUES (1.2,                  1.2);
INSERT INTO t_float VALUES (1.2345,               1.2345);
INSERT INTO t_float VALUES (1.23456,              1.23456);
INSERT INTO t_float VALUES (1.234567,             1.234567);
INSERT INTO t_float VALUES (1.2345678,            1.2345678);
INSERT INTO t_float VALUES (1.23456789,           1.23456789);
INSERT INTO t_float VALUES (1.234567890,          1.234567890);
INSERT INTO t_float VALUES (1.2345678901,         1.2345678901);
INSERT INTO t_float VALUES (1.23456789012,        1.23456789012);
INSERT INTO t_float VALUES (1.234567890123,       1.234567890123);
INSERT INTO t_float VALUES (1.2345678901234,      1.2345678901234);
INSERT INTO t_float VALUES (1.23456789012345,     1.23456789012345);
INSERT INTO t_float VALUES (1.234567890123456,    1.234567890123456);
INSERT INTO t_float VALUES (1.2345678901234567,   1.2345678901234567);
INSERT INTO t_float VALUES (1.23456789012345678,  1.23456789012345678);


:explain_analyze SELECT * FROM t_float WHERE f4 = 1.23456789;
:explain_analyze SELECT * FROM t_float WHERE f4 < 1.23456789;
:explain_analyze SELECT * FROM t_float WHERE f4 > 1.23456789;

:explain_analyze SELECT * FROM t_float WHERE f4 = 1.2345678901234567;
:explain_analyze SELECT * FROM t_float WHERE f4 < 1.2345678901234567;
:explain_analyze SELECT * FROM t_float WHERE f4 > 1.2345678901234566;

:explain_analyze SELECT * FROM t_float WHERE f8 = 1.2345678901234567;
:explain_analyze SELECT * FROM t_float WHERE f8 < 1.2345678901234567;
:explain_analyze SELECT * FROM t_float WHERE f8 > 1.2345678901234566;

DROP TABLE t_float;

-- TODO: add tests for casting between dates and timestamp

----- text comparisons ---- (#24384)
CREATE TABLE t_name(v name);
CREATE TABLE t_text(v text);
CREATE INDEX t_name_range ON t_name(v ASC);
CREATE INDEX t_text_range ON t_text(v ASC);

INSERT INTO t_name VALUES ('aa'), ('ab'), ('ac'), ('ad'), ('ba'), ('bb'), ('bc'), ('bd');
INSERT INTO t_text VALUES ('aa'), ('ab'), ('ac'), ('ad'), ('ba'), ('bb'), ('bc'), ('bd');

:explain_analyze SELECT * FROM t_name WHERE v >= 'b'::name;
:explain_analyze SELECT * FROM t_name WHERE v >= 'b'::text;
:explain_analyze SELECT * FROM t_name WHERE v LIKE 'a%';

:explain_analyze SELECT * FROM t_text WHERE v >= 'b'::name;
:explain_analyze SELECT * FROM t_text WHERE v >= 'b'::text;
:explain_analyze SELECT * FROM t_text WHERE v LIKE 'a%';

DROP TABLE t_name, t_text;

----- domain comparisons ---- (#26726)
CREATE DOMAIN positive_int AS INT CHECK (VALUE > 0);
CREATE DOMAIN non_empty_text AS TEXT CHECK (VALUE <> '');

CREATE TABLE t_domain_int(placeholder int, i positive_int);
CREATE TABLE t_domain_text(placeholder int, t non_empty_text);

CREATE INDEX ON t_domain_int(i ASC);
CREATE INDEX ON t_domain_text(t ASC);

INSERT INTO t_domain_int SELECT i, i FROM generate_series(1, 10) i;
INSERT INTO t_domain_text SELECT i, i::text FROM generate_series(1, 10) i;

:explain_analyze SELECT * FROM t_domain_int WHERE i = 1;
:explain_analyze SELECT * FROM t_domain_text WHERE t = '1';

DROP TABLE t_domain_int, t_domain_text;
DROP DOMAIN positive_int, non_empty_text;

---
--- test a query that has multiple recheck types. 
---

CREATE TABLE test_multiple_rechecks (a int, b int, c int);
CREATE INDEX ON test_multiple_rechecks (a ASC, b ASC, c ASC);
INSERT INTO test_multiple_rechecks SELECT i, j, k FROM generate_series(1, 10) i, generate_series(1, 10) j, generate_series(1, 10) k;
:explain_analyze /*+ IndexScan(t) */ SELECT * FROM test_multiple_rechecks t WHERE ROW(a, b) < ROW(4, 4) AND c IN (1, 2, 3, 4, 5, 6) AND c < 4;

DROP TABLE test_multiple_rechecks;
