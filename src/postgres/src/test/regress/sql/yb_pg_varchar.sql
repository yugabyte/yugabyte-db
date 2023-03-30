--
-- VARCHAR
--

-- YB note: add ordering column to match upstream PG output ordering.  Also
-- adjust SELECTs below to not use * and instead use f1 in order to avoid
-- outputting up the ordering column.
CREATE TABLE VARCHAR_TBL(ybid serial, f1 varchar(1), PRIMARY KEY (ybid ASC));

INSERT INTO VARCHAR_TBL (f1) VALUES ('a');

INSERT INTO VARCHAR_TBL (f1) VALUES ('A');

-- any of the following three input formats are acceptable
INSERT INTO VARCHAR_TBL (f1) VALUES ('1');

INSERT INTO VARCHAR_TBL (f1) VALUES (2);

INSERT INTO VARCHAR_TBL (f1) VALUES ('3');

-- zero-length char
INSERT INTO VARCHAR_TBL (f1) VALUES ('');

-- try varchar's of greater than 1 length
INSERT INTO VARCHAR_TBL (f1) VALUES ('cd');
INSERT INTO VARCHAR_TBL (f1) VALUES ('c     ');


SELECT '' AS seven, f1 FROM VARCHAR_TBL;

SELECT '' AS six, c.f1
   FROM VARCHAR_TBL c
   WHERE c.f1 <> 'a';

SELECT '' AS one, c.f1
   FROM VARCHAR_TBL c
   WHERE c.f1 = 'a';

SELECT '' AS five, c.f1
   FROM VARCHAR_TBL c
   WHERE c.f1 < 'a';

SELECT '' AS six, c.f1
   FROM VARCHAR_TBL c
   WHERE c.f1 <= 'a';

SELECT '' AS one, c.f1
   FROM VARCHAR_TBL c
   WHERE c.f1 > 'a';

SELECT '' AS two, c.f1
   FROM VARCHAR_TBL c
   WHERE c.f1 >= 'a';

DROP TABLE VARCHAR_TBL;

--
-- Now test longer arrays of char
--

CREATE TABLE VARCHAR_TBL(f1 varchar(4));

INSERT INTO VARCHAR_TBL (f1) VALUES ('a');
INSERT INTO VARCHAR_TBL (f1) VALUES ('ab');
INSERT INTO VARCHAR_TBL (f1) VALUES ('abcd');
INSERT INTO VARCHAR_TBL (f1) VALUES ('abcde');
INSERT INTO VARCHAR_TBL (f1) VALUES ('abcd    ');

-- YB note: add query ordering to match upstream PG output ordering.
SELECT '' AS four, * FROM VARCHAR_TBL ORDER BY 1, 2;
