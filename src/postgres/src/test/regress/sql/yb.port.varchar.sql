--
-- VARCHAR
--

--
-- Build a table for testing
-- (This temporarily hides the table created in test_setup.sql)
-- YB note: but we also want to test YB tables instead of temporary tables, so
-- create a new table in a new namespace instead.
--
CREATE SCHEMA yb_tmp;
SET search_path TO yb_tmp;

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

-- YB note: reset the changes from above.
DROP SCHEMA yb_tmp;
RESET search_path;

--
-- Now test longer arrays of char
--
-- This varchar_tbl was already created and filled in test_setup.sql.
-- Here we just try to insert bad values.
--

INSERT INTO VARCHAR_TBL (f1) VALUES ('abcde');

-- YB note: add query ordering to match upstream PG output ordering.
SELECT '' AS four, * FROM VARCHAR_TBL ORDER BY 1, 2;
