--
-- CIRCLE
--

-- avoid bit-exact output here because operations may not be bit-exact.
SET extra_float_digits = 0;

CREATE TABLE CIRCLE_TBL (id int primary key, f1 circle);

INSERT INTO CIRCLE_TBL VALUES (1, '<(5,1),3>');

INSERT INTO CIRCLE_TBL VALUES (2, '<(1,2),100>');

INSERT INTO CIRCLE_TBL VALUES (3, '1,3,5');

INSERT INTO CIRCLE_TBL VALUES (4, '((1,2),3)');

INSERT INTO CIRCLE_TBL VALUES (5, '<(100,200),10>');

INSERT INTO CIRCLE_TBL VALUES (6, '<(100,1),115>');

-- bad values

INSERT INTO CIRCLE_TBL VALUES (7, '<(-100,0),-100>');

INSERT INTO CIRCLE_TBL VALUES (8, '1abc,3,5');

INSERT INTO CIRCLE_TBL VALUES (9, '(3,(1,2),3)');

SELECT * FROM CIRCLE_TBL ORDER BY id;

SELECT '' AS six, center(f1) AS center
  FROM CIRCLE_TBL ORDER BY id;

SELECT '' AS six, radius(f1) AS radius
  FROM CIRCLE_TBL ORDER BY id;

SELECT '' AS six, diameter(f1) AS diameter
  FROM CIRCLE_TBL ORDER BY id;

SELECT '' AS two, f1 FROM CIRCLE_TBL WHERE radius(f1) < 5 ORDER BY id;

SELECT '' AS four, f1 FROM CIRCLE_TBL WHERE diameter(f1) >= 10 ORDER BY id;

SELECT '' as five, c1.f1 AS one, c2.f1 AS two, (c1.f1 <-> c2.f1) AS distance
  FROM CIRCLE_TBL c1, CIRCLE_TBL c2
  WHERE (c1.f1 < c2.f1) AND ((c1.f1 <-> c2.f1) > 0)
  ORDER BY distance, area(c1.f1), area(c2.f1);
