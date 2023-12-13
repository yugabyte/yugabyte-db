--
-- POINT
--

-- avoid bit-exact output here because operations may not be bit-exact.
SET extra_float_digits = 0;

-- point_tbl was already created and filled in test_setup.sql.
-- Here we just try to insert bad values.

INSERT INTO POINT_TBL(f1) VALUES ('asdfasdf');

INSERT INTO POINT_TBL(f1) VALUES ('(10.0 10.0)');

INSERT INTO POINT_TBL(f1) VALUES ('(10.0,10.0');


SELECT '' AS six, * FROM POINT_TBL ORDER BY p.f1[0], p.f1[1];

-- left of
SELECT '' AS three, p.* FROM POINT_TBL p WHERE p.f1 << '(0.0, 0.0)' ORDER BY p.f1[0], p.f1[1];

-- right of
SELECT '' AS three, p.* FROM POINT_TBL p WHERE '(0.0,0.0)' >> p.f1 ORDER BY p.f1[0], p.f1[1];

-- above
SELECT '' AS one, p.* FROM POINT_TBL p WHERE '(0.0,0.0)' >^ p.f1 ORDER BY p.f1[0], p.f1[1];

-- below
SELECT '' AS one, p.* FROM POINT_TBL p WHERE p.f1 <^ '(0.0, 0.0)' ORDER BY p.f1[0], p.f1[1];

-- equal
SELECT '' AS one, p.* FROM POINT_TBL p WHERE p.f1 ~= '(5.1, 34.5)' ORDER BY p.f1[0], p.f1[1];

-- point in box
SELECT '' AS three, p.* FROM POINT_TBL p
   WHERE p.f1 <@ box '(0,0,100,100)' ORDER BY p.f1[0], p.f1[1];

SELECT '' AS three, p.* FROM POINT_TBL p
   WHERE box '(0,0,100,100)' @> p.f1 ORDER BY p.f1[0], p.f1[1];

SELECT '' AS three, p.* FROM POINT_TBL p
   WHERE not p.f1 <@ box '(0,0,100,100)' ORDER BY p.f1[0], p.f1[1];

SELECT '' AS two, p.* FROM POINT_TBL p
   WHERE p.f1 <@ path '[(0,0),(-10,0),(-10,10)]' ORDER BY p.f1[0], p.f1[1];

SELECT '' AS three, p.* FROM POINT_TBL p
   WHERE not box '(0,0,100,100)' @> p.f1 ORDER BY p.f1[0], p.f1[1];

SELECT '' AS six, p.f1, p.f1 <-> point '(0,0)' AS dist
   FROM POINT_TBL p
   ORDER BY dist;

SELECT '' AS thirtysix, p1.f1 AS point1, p2.f1 AS point2, p1.f1 <-> p2.f1 AS dist
   FROM POINT_TBL p1, POINT_TBL p2
   ORDER BY dist, p1.f1[0], p1.f1[1], p2.f1[0], p2.f1[1];

SELECT '' AS thirty, p1.f1 AS point1, p2.f1 AS point2
   FROM POINT_TBL p1, POINT_TBL p2
   WHERE (p1.f1 <-> p2.f1) > 3 ORDER BY p1.f1[0], p1.f1[1], p2.f1[0], p2.f1[1];

-- put distance result into output to allow sorting with GEQ optimizer - tgl 97/05/10
SELECT '' AS fifteen, p1.f1 AS point1, p2.f1 AS point2, (p1.f1 <-> p2.f1) AS distance
   FROM POINT_TBL p1, POINT_TBL p2
   WHERE (p1.f1 <-> p2.f1) > 3 and p1.f1 << p2.f1
   ORDER BY distance, p1.f1[0], p2.f1[0];

-- put distance result into output to allow sorting with GEQ optimizer - tgl 97/05/10
SELECT '' AS three, p1.f1 AS point1, p2.f1 AS point2, (p1.f1 <-> p2.f1) AS distance
   FROM POINT_TBL p1, POINT_TBL p2
   WHERE (p1.f1 <-> p2.f1) > 3 and p1.f1 << p2.f1 and p1.f1 >^ p2.f1
   ORDER BY distance;

-- TODO(neil) Once temp table is supported, enable the following test case.
-- Test that GiST indexes provide same behavior as sequential scan
-- CREATE TEMP TABLE point_gist_tbl(f1 point);
CREATE TABLE point_gist_tbl(f1 point);
INSERT INTO point_gist_tbl SELECT '(0,0)' FROM generate_series(0,1000);
-- INDEX non-empty table not supported.
-- CREATE INDEX point_gist_tbl_index ON point_gist_tbl USING gist (f1);
INSERT INTO point_gist_tbl VALUES ('(0.0000009,0.0000009)');
SET enable_seqscan TO true;
SET enable_indexscan TO false;
SET enable_bitmapscan TO false;
SELECT COUNT(*) FROM point_gist_tbl WHERE f1 ~= '(0.0000009,0.0000009)'::point;
SELECT COUNT(*) FROM point_gist_tbl WHERE f1 <@ '(0.0000009,0.0000009),(0.0000009,0.0000009)'::box;
SELECT COUNT(*) FROM point_gist_tbl WHERE f1 ~= '(0.0000018,0.0000018)'::point;
SET enable_seqscan TO false;
SET enable_indexscan TO true;
SET enable_bitmapscan TO true;
SELECT COUNT(*) FROM point_gist_tbl WHERE f1 ~= '(0.0000009,0.0000009)'::point;
SELECT COUNT(*) FROM point_gist_tbl WHERE f1 <@ '(0.0000009,0.0000009),(0.0000009,0.0000009)'::box;
SELECT COUNT(*) FROM point_gist_tbl WHERE f1 ~= '(0.0000018,0.0000018)'::point;
RESET enable_seqscan;
RESET enable_indexscan;
RESET enable_bitmapscan;
