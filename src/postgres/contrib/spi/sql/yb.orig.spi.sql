-- Test autoinc EXTENSION

CREATE EXTENSION autoinc;

CREATE SEQUENCE next_id START -2 MINVALUE -2;

CREATE TABLE ids (id int4, idesc text);

CREATE TRIGGER ids_nextid
	BEFORE INSERT OR UPDATE ON ids
	FOR EACH ROW
	EXECUTE PROCEDURE autoinc (id, next_id);

INSERT INTO ids VALUES (0, 'first (-2 ?)');
INSERT INTO ids VALUES (null, 'second (-1 ?)');
INSERT INTO ids(idesc) VALUES ('third (1 ?!)');

SELECT * FROM ids ORDER BY idesc ASC;

UPDATE ids SET id = null, idesc = 'first: -2 --> 2' WHERE idesc = 'first (-2 ?)';
UPDATE ids SET id = 0, idesc = 'second: -1 --> 3' WHERE id = -1;
UPDATE ids SET id = 4, idesc = 'third: 1 --> 4' WHERE id = 1;

SELECT * FROM ids ORDER BY idesc ASC;

SELECT 'Wasn''t it 4 ?' as nextval, nextval ('next_id');

DROP SEQUENCE next_id;
DROP TABLE ids;

-- Test insert_username EXTENSION

CREATE EXTENSION insert_username;

CREATE TABLE username_test (name text, username text not null);

CREATE TRIGGER insert_usernames
	BEFORE INSERT OR UPDATE ON username_test
	FOR EACH ROW
	EXECUTE PROCEDURE insert_username (username);

INSERT INTO username_test VALUES ('nothing');
INSERT INTO username_test VALUES ('null', null);
INSERT INTO username_test VALUES ('empty string', '');
INSERT INTO username_test VALUES ('space', ' ');
INSERT INTO username_test VALUES ('tab', '	');
INSERT INTO username_test VALUES ('name', 'name');

SELECT * FROM username_test ORDER BY name ASC;

DROP TABLE username_test;

-- Test moddatetime EXTENSION

CREATE EXTENSION moddatetime;

CREATE TABLE mdt (id int4, idesc text, moddate timestamp DEFAULT CURRENT_TIMESTAMP NOT NULL);

CREATE TRIGGER mdt_moddatetime
	BEFORE UPDATE ON mdt
	FOR EACH ROW
	EXECUTE PROCEDURE moddatetime (moddate);

INSERT INTO mdt VALUES (1, 'first');
INSERT INTO mdt VALUES (2, 'second');
INSERT INTO mdt VALUES (3, 'third');

SELECT id, idesc FROM mdt ORDER BY moddate;

UPDATE mdt SET id = 4 WHERE id = 1;
UPDATE mdt SET id = 5 WHERE id = 2;
UPDATE mdt SET id = 6 WHERE id = 3;

SELECT id, idesc FROM mdt ORDER BY moddate ASC;

DROP TABLE mdt;

CREATE TABLE spi_test (
  id int primary key,
  content text,
  username text not null,
  moddate timestamp DEFAULT CURRENT_TIMESTAMP NOT NULL
);

CREATE TRIGGER insert_usernames
  BEFORE INSERT OR UPDATE ON spi_test
  FOR EACH ROW
  EXECUTE PROCEDURE insert_username (username);

CREATE TRIGGER update_moddatetime
  BEFORE UPDATE ON spi_test
  FOR EACH ROW
  EXECUTE PROCEDURE moddatetime (moddate);

SET ROLE yugabyte;
INSERT INTO spi_test VALUES(1, 'desc1');

SET ROLE postgres;
INSERT INTO spi_test VALUES(2, 'desc2');
INSERT INTO spi_test VALUES(3, 'desc3');

SET ROLE yugabyte;
INSERT INTO spi_test VALUES(4, 'desc4');

SELECT  id, content, username FROM spi_test ORDER BY moddate ASC;

UPDATE spi_test SET content = 'desc1_updated' WHERE id = 1;
UPDATE spi_test SET content = 'desc3_updated' WHERE id = 3;

SELECT id, content, username FROM spi_test ORDER BY moddate ASC;

DROP TABLE username_test;

-- Test refint EXTENSION

CREATE EXTENSION refint;

--Column ID of table A is primary key:

CREATE TABLE A ( id int4 not null);
CREATE UNIQUE INDEX AI ON A (id);

--Columns REFB of table B and REFC of C are foreign keys referencing ID of A:

CREATE TABLE B (refb int4);
CREATE INDEX BI ON B (refb);

CREATE TABLE C (refc int4);
CREATE INDEX CI ON C (refc);

--Trigger for table A:

CREATE TRIGGER AT BEFORE DELETE OR UPDATE ON A FOR EACH ROW
EXECUTE PROCEDURE check_foreign_key (2, 'cascade', 'id', 'B', 'refb', 'C', 'refc');

-- 2	- means that check must be performed for foreign keys of 2 tables.
-- cascade	- defines that corresponding keys must be deleted.
-- ID	- name of primary key column in triggered table (A). You may use as many columns as you need.
-- B	- name of (first) table with foreign keys.
-- REFB	- name of foreign key column in this table. You may use as many columns as you need, but
-- number of key columns in referenced table (A) must be the same.
-- C	- name of second table with foreign keys.
-- REFC	- name of foreign key column in this table.

--Trigger for table B:

CREATE TRIGGER BT BEFORE INSERT OR UPDATE ON B FOR EACH ROW
EXECUTE PROCEDURE check_primary_key ('refb', 'A', 'id');

-- REFB	- name of foreign key column in triggered (B) table. You may use as many columns as you need,
-- but number of key columns in referenced table must be the same.
-- A	- referenced table name.
-- ID	- name of primary key column in referenced table.

--Trigger for table C:

CREATE TRIGGER CT BEFORE INSERT OR UPDATE ON C FOR EACH ROW
EXECUTE PROCEDURE check_primary_key ('refc', 'A', 'id');

INSERT INTO A VALUES (10);
INSERT INTO A VALUES (20);
INSERT INTO A VALUES (30);
INSERT INTO A VALUES (40);
INSERT INTO A VALUES (50);

-- invalid reference
INSERT INTO B VALUES (1);

INSERT INTO B VALUES (10);
INSERT INTO B VALUES (30);
INSERT INTO B VALUES (30);

-- invalid reference
INSERT INTO C VALUES (11);

INSERT INTO C VALUES (20);
INSERT INTO C VALUES (20);
INSERT INTO C VALUES (30);

DELETE FROM A WHERE id = 10;
DELETE FROM A WHERE id = 20;
DELETE FROM A WHERE id = 30;

SELECT * FROM A ORDER BY id ASC;
SELECT * FROM B ORDER BY refb ASC;
SELECT * FROM C ORDER BY refc ASC;

DROP TABLE A;
DROP TABLE B;
DROP TABLE C;
