-- Verify yb_db_admin can alter rows in tables it does not own
CREATE TABLE foo(a INT);
INSERT INTO foo VALUES (0);
INSERT INTO foo VALUES (1);
INSERT INTO foo VALUES (2);
SET SESSION AUTHORIZATION yb_db_admin;
SELECT * from foo ORDER BY a;
DELETE FROM foo WHERE a = 2;
SELECT * from foo ORDER BY a;
UPDATE foo SET a = 5 WHERE a = 0;  
SELECT * from foo ORDER BY a;
TRUNCATE foo;
SELECT * from foo ORDER BY a;
-- Check yb_db_admin cannot alter sys tables
UPDATE pg_shdepend SET dbid=1 WHERE dbid=0;
DELETE from pg_shdepend WHERE dbid=0;
TRUNCATE pg_shdepend;
