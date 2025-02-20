--
-- INT2
--

-- int2_tbl was already created and filled in test_setup.sql.
-- Here we just try to insert bad values.

INSERT INTO INT2_TBL(f1) VALUES ('34.5');
INSERT INTO INT2_TBL(f1) VALUES ('100000');
INSERT INTO INT2_TBL(f1) VALUES ('asdf');
INSERT INTO INT2_TBL(f1) VALUES ('    ');
INSERT INTO INT2_TBL(f1) VALUES ('- 1234');
INSERT INTO INT2_TBL(f1) VALUES ('4 444');
INSERT INTO INT2_TBL(f1) VALUES ('123 dt');
INSERT INTO INT2_TBL(f1) VALUES ('');


SELECT '' AS five, * FROM INT2_TBL ORDER BY f1;

SELECT '' AS four, i.* FROM INT2_TBL i WHERE i.f1 <> int2 '0' ORDER BY i.f1;

SELECT '' AS four, i.* FROM INT2_TBL i WHERE i.f1 <> int4 '0' ORDER BY i.f1;

SELECT '' AS one, i.* FROM INT2_TBL i WHERE i.f1 = int2 '0' ORDER BY i.f1;

SELECT '' AS one, i.* FROM INT2_TBL i WHERE i.f1 = int4 '0' ORDER BY i.f1;

SELECT '' AS two, i.* FROM INT2_TBL i WHERE i.f1 < int2 '0' ORDER BY i.f1;

SELECT '' AS two, i.* FROM INT2_TBL i WHERE i.f1 < int4 '0' ORDER BY i.f1;

SELECT '' AS three, i.* FROM INT2_TBL i WHERE i.f1 <= int2 '0' ORDER BY i.f1;

SELECT '' AS three, i.* FROM INT2_TBL i WHERE i.f1 <= int4 '0' ORDER BY i.f1;

SELECT '' AS two, i.* FROM INT2_TBL i WHERE i.f1 > int2 '0' ORDER BY i.f1;

SELECT '' AS two, i.* FROM INT2_TBL i WHERE i.f1 > int4 '0' ORDER BY i.f1;

SELECT '' AS three, i.* FROM INT2_TBL i WHERE i.f1 >= int2 '0' ORDER BY i.f1;

SELECT '' AS three, i.* FROM INT2_TBL i WHERE i.f1 >= int4 '0' ORDER BY i.f1;

-- positive odds
SELECT '' AS one, i.* FROM INT2_TBL i WHERE (i.f1 % int2 '2') = int2 '1' ORDER BY i.f1;

-- any evens
SELECT '' AS three, i.* FROM INT2_TBL i WHERE (i.f1 % int4 '2') = int2 '0' ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 * int2 '2' AS x FROM INT2_TBL i ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 * int2 '2' AS x FROM INT2_TBL i
WHERE abs(f1) < 16384 ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 * int4 '2' AS x FROM INT2_TBL i ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 + int2 '2' AS x FROM INT2_TBL i ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 + int2 '2' AS x FROM INT2_TBL i
WHERE f1 < 32766 ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 + int4 '2' AS x FROM INT2_TBL i ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 - int2 '2' AS x FROM INT2_TBL i ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 - int2 '2' AS x FROM INT2_TBL i
WHERE f1 > -32767 ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 - int4 '2' AS x FROM INT2_TBL i ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 / int2 '2' AS x FROM INT2_TBL i ORDER BY i.f1;

SELECT '' AS five, i.f1, i.f1 / int4 '2' AS x FROM INT2_TBL i ORDER BY i.f1;

-- corner cases
SELECT (-1::int2<<15)::text;
SELECT ((-1::int2<<15)+1::int2)::text;

-- check sane handling of INT16_MIN overflow cases
SELECT (-32768)::int2 * (-1)::int2;
SELECT (-32768)::int2 / (-1)::int2;
SELECT (-32768)::int2 % (-1)::int2;

-- check rounding when casting from float
SELECT x, x::int2 AS int2_value
FROM (VALUES (-2.5::float8),
             (-1.5::float8),
             (-0.5::float8),
             (0.0::float8),
             (0.5::float8),
             (1.5::float8),
             (2.5::float8)) t(x);

-- check rounding when casting from numeric
SELECT x, x::int2 AS int2_value
FROM (VALUES (-2.5::numeric),
             (-1.5::numeric),
             (-0.5::numeric),
             (0.0::numeric),
             (0.5::numeric),
             (1.5::numeric),
             (2.5::numeric)) t(x);
