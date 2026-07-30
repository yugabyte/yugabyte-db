--
-- Assure that some of the record tests taken from `plpgsql.sql` work not just
-- for temporary tables but also regular tables.  (Functions are taken from
-- `plpgsql.sql`.)
--
create temp table foo (f1 int, f2 int);
insert into foo values (1, 2), (3, 4), (5, 6), (5, 6), (7, 8), (9, 10);
select * from foo order by f1;

create table bar (f1 int, f2 int);
insert into bar values (1, 2), (3, 4), (5, 6), (5, 6), (7, 8), (9, 10);
select * from bar order by f1;

create or replace function stricttest1() returns void as $$
declare x record;
begin
  -- should work
  select * from foo where f1 = 3 into strict x;
  raise notice 'x.f1 = %, x.f2 = %', x.f1, x.f2;
end$$ language plpgsql;
create or replace function stricttest2() returns void as $$
declare x record;
begin
  -- should work
  select * from bar where f1 = 3 into strict x;
  raise notice 'x.f1 = %, x.f2 = %', x.f1, x.f2;
end$$ language plpgsql;
create or replace function stricttest3() returns void as $$
declare x record;
begin
  -- too many rows, no params
  select * from foo where f1 > 3 into strict x;
  raise notice 'x.f1 = %, x.f2 = %', x.f1, x.f2;
end$$ language plpgsql;
create or replace function stricttest4() returns void as $$
declare x record;
begin
  -- too many rows, no params
  select * from bar where f1 > 3 into strict x;
  raise notice 'x.f1 = %, x.f2 = %', x.f1, x.f2;
end$$ language plpgsql;
select stricttest1();

select stricttest2();

select stricttest3();
select stricttest4();
--
-- Cleanup
--
DROP TABLE foo;
DROP TABLE bar;
DROP FUNCTION stricttest1(), stricttest2(), stricttest3(), stricttest4();

-- Fail because collate and cursor are not supported.
create or replace function unsupported1() returns void as $$
declare a text collate "en_US";
begin
end$$ language plpgsql;

create table test(k int, v int);

create procedure intermediate_commit() as $$
begin
  insert into test values(1, 1);
  insert into test values(2, 2);
  commit;
  insert into test values(3, 3);
end$$ LANGUAGE plpgsql;

call intermediate_commit();

select * from test order by k;

do $$
begin
  insert into test values(4, 4);
  commit;
  insert into test values(5, 5);
  insert into test values(6, 6);
end $$;

select * from test order by k;

do $$
begin
  insert into test values(7, 7);
  -- commit inserting (7, 7) row and start new transaction automatically.
  commit;
  insert into test values(8, 8);
  rollback;
  -- only insertion of (8, 8) row is rolled back.
  -- new transaction is started automatically, next row will be inserted.
  insert into test values(9, 9);
end $$;

select * from test order by k;

create procedure p(a inout int)
  language plpgsql
as $body$
begin
  a := a + 1;
end;
$body$;

do $body$
declare
  a int := 10;
begin
  call p(a);
  raise info '%', a::text;
end;
$body$;
-- check exit out of outermost block
do $$
<<outerblock>>
begin
  <<innerblock>>
  begin
    exit outerblock;
    raise notice 'should not get here';
  end;
  raise notice 'should not get here, either';
end$$;

-- Self replacing function test
CREATE OR REPLACE FUNCTION f1(int)
RETURNS FLOAT8 AS $$
DECLARE
    -- Define the DDL statement for the new version of the function as a single text string
    ddl_statement TEXT := $Q$
        CREATE OR REPLACE FUNCTION f1(int) RETURNS FLOAT8 AS $F$
        BEGIN
            -- This is the code for the NEW version (V2)
            RETURN 2.0::FLOAT8 / $1;
        END;
        $F$ LANGUAGE plpgsql;
    $Q$;
BEGIN
    -- 1. Execute the DDL dynamically. This replaces the function for the next call.
    EXECUTE ddl_statement;

    -- 2. Execute the code for the CURRENT version (V1)
    RETURN 1.0::FLOAT8 / $1;
END;
$$ LANGUAGE plpgsql;

-- Step 1: Create table t1 by calling V1 (which also creates V2)
CREATE TABLE t1 AS SELECT f1(1);

-- Step 2: The table T1 stores the result of V1
SELECT * FROM t1;

-- Step 3: Call the function again. It will now execute V2.
SELECT f1(2);

-- Self replacing procedure test
CREATE TABLE proc_log (
    run_order INT,
    msg TEXT
);

CREATE OR REPLACE PROCEDURE self_modifying_proc()
LANGUAGE plpgsql
AS $$
DECLARE
    -- The definition for Version 2
    v_new_definition TEXT := $Q$
        CREATE OR REPLACE PROCEDURE self_modifying_proc()
        LANGUAGE plpgsql
        AS $V2$
        BEGIN
            -- Version 2 inserts '2' as the order
            INSERT INTO proc_log (run_order, msg) VALUES (2, 'v2-executed');
        END;
        $V2$;
    $Q$;
BEGIN
    -- Version 1 logic
    EXECUTE v_new_definition;

    -- Version 1 inserts '1' as the order
    INSERT INTO proc_log (run_order, msg) VALUES (1, 'v1-executed');
END;
$$;

-- First execution (runs V1, creates V2)
CALL self_modifying_proc();

-- Second execution (runs V2)
CALL self_modifying_proc();

-- Final verification for your Unit Test assertion
SELECT run_order, msg FROM proc_log ORDER BY run_order;

-- Create or replace function multiple times within a transaction block
-- Initial Setup
CREATE OR REPLACE FUNCTION version_test() RETURNS TEXT AS $$
BEGIN
    RETURN 'Initial Version';
END;
$$ LANGUAGE plpgsql;

BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;

    -- 1. Call the initial version
    SELECT version_test() AS run_1;

    -- 2. Replace with Version A
    CREATE OR REPLACE FUNCTION version_test() RETURNS TEXT AS $$
    BEGIN
        RETURN 'Version A (Modified inside TX)';
    END;
    $$ LANGUAGE plpgsql;

    -- 3. Call again - PostgreSQL will see the new definition
    SELECT version_test() AS run_2;

    -- 4. Replace with Version B
    CREATE OR REPLACE FUNCTION version_test() RETURNS TEXT AS $$
    BEGIN
        RETURN 'Version B (Final inside TX)';
    END;
    $$ LANGUAGE plpgsql;

    -- 5. Call one last time
    SELECT version_test() AS run_3;

COMMIT;

-- 6. Call outside the ddl transaction block
SELECT version_test() AS run_4;

-- Create or replace procedure multiple times within a transaction block
-- Initial Setup: Create the log table and the first version of the procedure
CREATE TABLE procedure_test_log (run_id INT, output TEXT);

CREATE OR REPLACE PROCEDURE multi_version_proc(p_id INT)
LANGUAGE plpgsql
AS $$
BEGIN
    INSERT INTO procedure_test_log VALUES (p_id, 'Initial Version');
END;
$$;

-- Start the Repeatable Read Transaction
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;

    -- 1. Call the initial version
    CALL multi_version_proc(1);

    -- 2. Replace with Version A
    CREATE OR REPLACE PROCEDURE multi_version_proc(p_id INT)
    LANGUAGE plpgsql
    AS $$
    BEGIN
        INSERT INTO procedure_test_log VALUES (p_id, 'Version A - Modified in TX');
    END;
    $$;

    -- 3. Call again - The session cache is invalidated and reloads the new DDL
    CALL multi_version_proc(2);

    -- 4. Replace with Version B
    CREATE OR REPLACE PROCEDURE multi_version_proc(p_id INT)
    LANGUAGE plpgsql
    AS $$
    BEGIN
        INSERT INTO procedure_test_log VALUES (p_id, 'Version B - Final in TX');
    END;
    $$;

    -- 5. Call the final version
    CALL multi_version_proc(3);

COMMIT;

-- 6. Call the final version outside the ddl transaction block
CALL multi_version_proc(4);

-- Verification for Unit Test
SELECT * FROM procedure_test_log ORDER BY run_id;

--- Test ALTER ... SET SCHEMA
CREATE SCHEMA schema1;
ALTER PROCEDURE intermediate_commit SET SCHEMA schema1;
call schema1.intermediate_commit();

ALTER ROUTINE f1 SET SCHEMA schema1;
SELECT schema1.f1(2);

--- Test ALTER ROUTINE ... SET/RESET
SELECT current_setting('work_mem');

CREATE FUNCTION report_guc(text) RETURNS text AS
$$ select current_setting($1) $$ LANGUAGE SQL
SET work_mem = '1MB';

SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) SET work_mem = '2MB';
SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) SET work_mem = DEFAULT;
SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) SET work_mem FROM CURRENT;
SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) RESET work_mem;
SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) RESET ALL;
SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) SET work_mem = '2MB' RESTRICT;
SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) SET work_mem = DEFAULT RESTRICT;
SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) SET work_mem FROM CURRENT RESTRICT;
SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) RESET work_mem RESTRICT;
SELECT report_guc('work_mem'), current_setting('work_mem');

ALTER ROUTINE report_guc(text) RESET ALL RESTRICT;
SELECT report_guc('work_mem'), current_setting('work_mem');

--- Test ALTER PROCEDURE ... SET/RESET
CREATE PROCEDURE print_guc(text) AS
$$
begin
  raise info '%', current_setting($1);
end;
$$ LANGUAGE plpgsql
SET work_mem = '1MB';

call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) SET work_mem = '2MB';
call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) SET work_mem = DEFAULT;
call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) SET work_mem FROM CURRENT;
call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) RESET work_mem;
call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) RESET ALL;
call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) SET work_mem = '2MB' RESTRICT;
call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) SET work_mem = DEFAULT RESTRICT;
call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) SET work_mem FROM CURRENT RESTRICT;
call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) RESET work_mem RESTRICT;
call print_guc('work_mem');

ALTER PROCEDURE print_guc(text) RESET ALL RESTRICT;
call print_guc('work_mem');

--- Test ALTER { ROUTINE | PROCEDURE } ... SECURITY { DEFINER | INVOKER }
CREATE TABLE secret_data (id int, info text);
INSERT INTO secret_data VALUES (1, 'Top Secret');
SELECT * FROM secret_data;

CREATE OR REPLACE PROCEDURE change_data(new_info text) AS
$$BEGIN
    UPDATE secret_data SET info = new_info WHERE id = 1;
END$$ LANGUAGE plpgsql;
ALTER PROCEDURE change_data(text) SET search_path = public;

CREATE ROLE worker;
REVOKE ALL ON secret_data FROM worker;
GRANT EXECUTE ON PROCEDURE change_data(text) TO worker;

SET ROLE worker;
CALL change_data('Hacked by worker');  -- error: permission denied
RESET ROLE;
SELECT * FROM secret_data;

ALTER PROCEDURE change_data(text) SECURITY DEFINER;
ALTER PROCEDURE change_data(text) SECURITY DEFINER RESTRICT;
ALTER PROCEDURE change_data(text) EXTERNAL SECURITY DEFINER;
ALTER PROCEDURE change_data(text) EXTERNAL SECURITY DEFINER RESTRICT;
ALTER ROUTINE change_data(text) SECURITY DEFINER;
ALTER ROUTINE change_data(text) SECURITY DEFINER RESTRICT;
ALTER ROUTINE change_data(text) EXTERNAL SECURITY DEFINER;
ALTER ROUTINE change_data(text) EXTERNAL SECURITY DEFINER RESTRICT;

SET ROLE worker;
CALL change_data('Changed via Security Definer');
RESET ROLE;
SELECT * FROM secret_data;

ALTER PROCEDURE change_data(text) SECURITY INVOKER;
ALTER PROCEDURE change_data(text) SECURITY INVOKER RESTRICT;
ALTER PROCEDURE change_data(text) EXTERNAL SECURITY INVOKER;
ALTER PROCEDURE change_data(text) EXTERNAL SECURITY INVOKER RESTRICT;
ALTER ROUTINE change_data(text) SECURITY INVOKER;
ALTER ROUTINE change_data(text) SECURITY INVOKER RESTRICT;
ALTER ROUTINE change_data(text) EXTERNAL SECURITY INVOKER;
ALTER ROUTINE change_data(text) EXTERNAL SECURITY INVOKER RESTRICT;

SET ROLE worker;
CALL change_data('Try Security Invoker');  -- error: permission denied
RESET ROLE;
SELECT * FROM secret_data;
