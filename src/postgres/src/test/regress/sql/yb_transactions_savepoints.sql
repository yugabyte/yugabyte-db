--
-- SAVEPOINTS
--

-- Subtransactions, basic tests
CREATE TABLE trans_foobar (a int);
CREATE TABLE trans_foo (a int);
CREATE TABLE trans_baz (a int);
CREATE TABLE trans_barbaz (a int);

-- should exist: trans_barbaz, trans_baz, trans_foo
SELECT * FROM trans_foo;		-- should be empty
SELECT * FROM trans_bar;		-- shouldn't exist
SELECT * FROM trans_barbaz;	-- should be empty
SELECT * FROM trans_baz;		-- should be empty

-- inserts
BEGIN;
	INSERT INTO trans_foo VALUES (1);
	SAVEPOINT one;
		INSERT into trans_bar VALUES (1);
	ROLLBACK TO one;
	RELEASE SAVEPOINT one;
	SAVEPOINT two;
		INSERT into trans_barbaz VALUES (1);
	RELEASE two;
	SAVEPOINT three;
		SAVEPOINT four;
			INSERT INTO trans_foo VALUES (2);
		RELEASE SAVEPOINT four;
	ROLLBACK TO SAVEPOINT three;
	RELEASE SAVEPOINT three;
	INSERT INTO trans_foo VALUES (3);
COMMIT;
SELECT * FROM trans_foo ORDER BY a ASC;		-- should have 1 and 3
SELECT * FROM trans_barbaz ORDER BY a ASC;	-- should have 1

-- test whole-tree commit
BEGIN;
	SAVEPOINT one;
		SELECT trans_foo;
	ROLLBACK TO SAVEPOINT one;
	RELEASE SAVEPOINT one;
	SAVEPOINT two;
		CREATE TABLE savepoints (a int);
		SAVEPOINT three;
			INSERT INTO savepoints VALUES (1);
			SAVEPOINT four;
				INSERT INTO savepoints VALUES (2);
				SAVEPOINT five;
					INSERT INTO savepoints VALUES (3);
				ROLLBACK TO SAVEPOINT five;
COMMIT;
COMMIT;		-- should not be in a transaction block
SELECT * FROM savepoints ORDER BY a ASC;

-- test whole-tree rollback
BEGIN;
	SAVEPOINT one;
		DELETE FROM savepoints WHERE a=1;
	RELEASE SAVEPOINT one;
	SAVEPOINT two;
		DELETE FROM savepoints WHERE a=1;
		SAVEPOINT three;
			DELETE FROM savepoints WHERE a=2;
ROLLBACK;
COMMIT;		-- should not be in a transaction block

SELECT * FROM savepoints ORDER BY a ASC;

-- test whole-tree commit on an aborted subtransaction
BEGIN;
	INSERT INTO savepoints VALUES (4);
	SAVEPOINT one;
		INSERT INTO savepoints VALUES (5);
		SELECT trans_foo;
COMMIT;
SELECT * FROM savepoints ORDER BY a ASC;

BEGIN;
	INSERT INTO savepoints VALUES (6);
	SAVEPOINT one;
		INSERT INTO savepoints VALUES (7);
	RELEASE SAVEPOINT one;
	INSERT INTO savepoints VALUES (8);
COMMIT;
-- rows 6 and 8 should have been created by the same xact
-- SELECT a.xmin = b.xmin FROM savepoints a, savepoints b WHERE a.a=6 AND b.a=8;
-- rows 6 and 7 should have been created by different xacts
-- SELECT a.xmin = b.xmin FROM savepoints a, savepoints b WHERE a.a=6 AND b.a=7;

BEGIN;
	INSERT INTO savepoints VALUES (9);
	SAVEPOINT one;
		INSERT INTO savepoints VALUES (10);
	ROLLBACK TO SAVEPOINT one;
		INSERT INTO savepoints VALUES (11);
COMMIT;
SELECT a FROM savepoints WHERE a in (9, 10, 11) ORDER BY a ASC;
-- rows 9 and 11 should have been created by different xacts
-- SELECT a.xmin = b.xmin FROM savepoints a, savepoints b WHERE a.a=9 AND b.a=11;

BEGIN;
	INSERT INTO savepoints VALUES (12);
	SAVEPOINT one;
		INSERT INTO savepoints VALUES (13);
		SAVEPOINT two;
			INSERT INTO savepoints VALUES (14);
	ROLLBACK TO SAVEPOINT one;
		INSERT INTO savepoints VALUES (15);
		SAVEPOINT two;
			INSERT INTO savepoints VALUES (16);
			SAVEPOINT three;
				INSERT INTO savepoints VALUES (17);
COMMIT;
SELECT a FROM savepoints WHERE a BETWEEN 12 AND 17 ORDER BY a ASC;

BEGIN;
	INSERT INTO savepoints VALUES (18);
	SAVEPOINT one;
		INSERT INTO savepoints VALUES (19);
		SAVEPOINT two;
			INSERT INTO savepoints VALUES (20);
	ROLLBACK TO SAVEPOINT one;
		INSERT INTO savepoints VALUES (21);
	ROLLBACK TO SAVEPOINT one;
		INSERT INTO savepoints VALUES (22);
COMMIT;
SELECT a FROM savepoints WHERE a BETWEEN 18 AND 22 ORDER BY a ASC;

DROP TABLE savepoints;

-- only in a transaction block:
SAVEPOINT one;
ROLLBACK TO SAVEPOINT one;
RELEASE SAVEPOINT one;

-- Only "rollback to" allowed in aborted state
BEGIN;
  SAVEPOINT one;
  SELECT 0/0;
  SAVEPOINT two;    -- ignored till the end of ...
  RELEASE SAVEPOINT one;      -- ignored till the end of ...
  ROLLBACK TO SAVEPOINT one;
  SELECT 1;
COMMIT;
SELECT 1;			-- this should work


-- test case for problems with revalidating an open relation during abort
create function inverse(int) returns float8 as
$$
begin
  analyze revalidate_bug;
  return 1::float8/$1;
exception
  when division_by_zero then return 0;
end$$ language plpgsql volatile;

create table revalidate_bug (c float8 unique);
insert into revalidate_bug values (1);
insert into revalidate_bug values (inverse(0));

drop table revalidate_bug;
drop function inverse(int);


-- verify that cursors created during an aborted subtransaction are
-- closed, but that we do not rollback the effect of any FETCHs
-- performed in the aborted subtransaction
begin;

savepoint x;
create table abc (a int);
insert into abc values (5);
insert into abc values (10);
declare foo cursor for select * from abc order by a asc;
fetch from foo;
rollback to x;

-- should fail
fetch from foo;
commit;

begin;

insert into abc values (5);
insert into abc values (10);
insert into abc values (15);
declare foo cursor for select * from abc order by a asc;

fetch from foo;

savepoint x;
fetch from foo;
rollback to x;

fetch from foo;

abort;


-- Test for proper cleanup after a failure in a cursor portal
-- that was created in an outer subtransaction
CREATE FUNCTION invert(x float8) RETURNS float8 LANGUAGE plpgsql AS
$$ begin return 1/x; end $$;

CREATE FUNCTION create_temp_tab() RETURNS text
LANGUAGE plpgsql AS $$
BEGIN
  CREATE TEMP TABLE new_table (f1 float8);
  -- case of interest is that we fail while holding an open
  -- relcache reference to new_table
  INSERT INTO new_table SELECT invert(0.0);
  RETURN 'foo';
END $$;

BEGIN;
DECLARE ok CURSOR FOR SELECT * FROM int8_tbl;
DECLARE ctt CURSOR FOR SELECT create_temp_tab();
FETCH ok;
SAVEPOINT s1;
FETCH ok;  -- should work
FETCH ctt; -- error occurs here
ROLLBACK TO s1;
FETCH ok;  -- should work
FETCH ctt; -- must be rejected
COMMIT;

DROP FUNCTION create_temp_tab();
DROP FUNCTION invert(x float8);


-- Test assorted behaviors around the implicit transaction block created
-- when multiple SQL commands are sent in a single Query message.  These
-- tests rely on the fact that psql will not break SQL commands apart at a
-- backslash-quoted semicolon, but will send them as one Query.

create temp table i_table (f1 int);

-- psql will show all results of a multi-statement Query
SELECT 1\; SELECT 2\; SELECT 3;

-- this implicitly commits:
insert into i_table values(1)\; select * from i_table;
-- 1/0 error will cause rolling back the whole implicit transaction
insert into i_table values(2)\; select * from i_table\; select 1/0;
select * from i_table;

rollback;  -- we are not in a transaction at this point

-- can use regular begin/commit/rollback within a single Query
begin\; insert into i_table values(3)\; commit;
rollback;  -- we are not in a transaction at this point
begin\; insert into i_table values(4)\; rollback;
rollback;  -- we are not in a transaction at this point

-- begin converts implicit transaction into a regular one that
-- can extend past the end of the Query
select 1\; begin\; insert into i_table values(5);
commit;
select 1\; begin\; insert into i_table values(6);
rollback;

-- commit in implicit-transaction state commits but issues a warning.
insert into i_table values(7)\; commit\; insert into i_table values(8)\; select 1/0;
-- similarly, rollback aborts but issues a warning.
insert into i_table values(9)\; rollback\; select 2;

select * from i_table;

rollback;  -- we are not in a transaction at this point

-- implicit transaction block is still a transaction block, for e.g. VACUUM
SELECT 1\; VACUUM;
SELECT 1\; COMMIT\; VACUUM;

-- we disallow savepoint-related commands in implicit-transaction state
SELECT 1\; SAVEPOINT sp;
SELECT 1\; COMMIT\; SAVEPOINT sp;
ROLLBACK TO SAVEPOINT sp\; SELECT 2;
SELECT 2\; RELEASE SAVEPOINT sp\; SELECT 3;

-- but this is OK, because the BEGIN converts it to a regular xact
SELECT 1\; BEGIN\; SAVEPOINT sp\; ROLLBACK TO SAVEPOINT sp\; COMMIT;


-- Test for successful cleanup of an aborted transaction at session exit.
-- THIS MUST BE THE LAST TEST IN THIS FILE.

begin;
select 1/0;
rollback to X;

-- DO NOT ADD ANYTHING HERE.
