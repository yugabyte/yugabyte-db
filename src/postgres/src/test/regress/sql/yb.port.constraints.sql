-- TODO: port other PG tests

CREATE TABLE UNIQUE_TBL (i int UNIQUE NULLS NOT DISTINCT, t text);

INSERT INTO UNIQUE_TBL VALUES (1, 'one');
INSERT INTO UNIQUE_TBL VALUES (2, 'two');
INSERT INTO UNIQUE_TBL VALUES (1, 'three');  -- fail
INSERT INTO UNIQUE_TBL VALUES (4, 'four');
INSERT INTO UNIQUE_TBL VALUES (5, 'one');
INSERT INTO UNIQUE_TBL (t) VALUES ('six');
INSERT INTO UNIQUE_TBL (t) VALUES ('seven');  -- fail
INSERT INTO UNIQUE_TBL (t) VALUES ('eight') ON CONFLICT DO NOTHING;  -- no-op

SELECT * FROM UNIQUE_TBL;

DROP TABLE UNIQUE_TBL;

--
-- Check constraints on INSERT
--

CREATE SEQUENCE INSERT_SEQ;

CREATE TABLE INSERT_TBL (x INT DEFAULT nextval('insert_seq'),
	y TEXT DEFAULT '-NULL-',
	z INT DEFAULT -1 * currval('insert_seq'),
	CONSTRAINT INSERT_TBL_CON CHECK (x >= 3 AND y <> 'check failed' AND x < 8),
	CHECK (x + z = 0));

INSERT INTO INSERT_TBL(x,z) VALUES (2, -2);

SELECT * FROM INSERT_TBL;

SELECT 'one' AS one, nextval('insert_seq');

INSERT INTO INSERT_TBL(y) VALUES ('Y');
INSERT INTO INSERT_TBL(y) VALUES ('Y');
INSERT INTO INSERT_TBL(x,z) VALUES (1, -2);
INSERT INTO INSERT_TBL(z,x) VALUES (-7,  7);
INSERT INTO INSERT_TBL VALUES (5, 'check failed', -5);
INSERT INTO INSERT_TBL VALUES (7, '!check failed', -7);
INSERT INTO INSERT_TBL(y) VALUES ('-!NULL-');

SELECT * FROM INSERT_TBL;

INSERT INTO INSERT_TBL(y,z) VALUES ('check failed', 4);
INSERT INTO INSERT_TBL(x,y) VALUES (5, 'check failed');
INSERT INTO INSERT_TBL(x,y) VALUES (5, '!check failed');
INSERT INTO INSERT_TBL(y) VALUES ('-!NULL-');

SELECT * FROM INSERT_TBL;

SELECT 'seven' AS one, nextval('insert_seq');

INSERT INTO INSERT_TBL(y) VALUES ('Y');

SELECT 'eight' AS one, currval('insert_seq');

-- According to SQL, it is OK to insert a record that gives rise to NULL
-- constraint-condition results.  Postgres used to reject this, but it
-- was wrong:
INSERT INTO INSERT_TBL VALUES (null, null, null);

SELECT * FROM INSERT_TBL;


--
-- Check inheritance of defaults and constraints
--

CREATE TABLE INSERT_CHILD (cx INT default 42,
	cy INT CHECK (cy > x))
	INHERITS (INSERT_TBL);

INSERT INTO INSERT_CHILD(x,z,cy) VALUES (7,-7,11);
INSERT INTO INSERT_CHILD(x,z,cy) VALUES (7,-7,6);
INSERT INTO INSERT_CHILD(x,z,cy) VALUES (6,-7,7);
INSERT INTO INSERT_CHILD(x,y,z,cy) VALUES (6,'check failed',-6,7);

SELECT * FROM INSERT_CHILD;

DROP TABLE INSERT_CHILD;

--
-- Check NO INHERIT type of constraints and inheritance
--

CREATE TABLE ATACC1 (TEST INT
	CHECK (TEST > 0) NO INHERIT);

CREATE TABLE ATACC2 (TEST2 INT) INHERITS (ATACC1);
-- check constraint is not there on child
INSERT INTO ATACC2 (TEST) VALUES (-3);
-- check constraint is there on parent
INSERT INTO ATACC1 (TEST) VALUES (-3);
DROP TABLE ATACC1 CASCADE;

CREATE TABLE ATACC1 (TEST INT, TEST2 INT
	CHECK (TEST > 0), CHECK (TEST2 > 10) NO INHERIT);

CREATE TABLE ATACC2 () INHERITS (ATACC1);
-- check constraint is there on child
INSERT INTO ATACC2 (TEST) VALUES (-3);
-- check constraint is there on parent
INSERT INTO ATACC1 (TEST) VALUES (-3);
-- check constraint is not there on child
INSERT INTO ATACC2 (TEST2) VALUES (3);
-- check constraint is there on parent
INSERT INTO ATACC1 (TEST2) VALUES (3);
DROP TABLE ATACC1 CASCADE;
