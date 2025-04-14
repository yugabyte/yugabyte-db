-- This test's output has temporary tables' schema names that contain the
-- tserver uuid, which can lead to unstable results.
-- Use replace_temp_schema_name to change the schema name to pg_temp_x so that
-- the result is stable.
select current_setting('data_directory') || '/describe.out' as desc_output_file
\gset
\set replace_temp_schema_name 'select regexp_replace(pg_read_file(:\'desc_output_file\'), \'pg_temp_.{32}_\\d+\', \'pg_temp_x\', \'g\')'

-- Test views on temporary tables.
create temp table main_table(a int, b int);

CREATE OR REPLACE FUNCTION trigger_func() RETURNS trigger LANGUAGE plpgsql AS '
BEGIN
	RAISE NOTICE ''trigger_func(%) called: action = %, when = %, level = %'', TG_ARGV[0], TG_OP, TG_WHEN, TG_LEVEL;
	RETURN NULL;
END;';

CREATE TRIGGER before_upd_a_row_trig BEFORE UPDATE OF a ON main_table
FOR EACH ROW EXECUTE PROCEDURE trigger_func('before_upd_a_row');
CREATE TRIGGER after_upd_b_row_trig AFTER UPDATE OF b ON main_table
FOR EACH ROW EXECUTE PROCEDURE trigger_func('after_upd_b_row');
CREATE TRIGGER after_upd_a_b_row_trig AFTER UPDATE OF a, b ON main_table
FOR EACH ROW EXECUTE PROCEDURE trigger_func('after_upd_a_b_row');

CREATE TRIGGER before_upd_a_stmt_trig BEFORE UPDATE OF a ON main_table
FOR EACH STATEMENT EXECUTE PROCEDURE trigger_func('before_upd_a_stmt');
CREATE TRIGGER after_upd_b_stmt_trig AFTER UPDATE OF b ON main_table
FOR EACH STATEMENT EXECUTE PROCEDURE trigger_func('after_upd_b_stmt');

CREATE temp VIEW main_view AS SELECT a, b FROM main_table;

-- VIEW trigger function
CREATE OR REPLACE FUNCTION view_trigger() RETURNS trigger
LANGUAGE plpgsql AS $$
declare
    argstr text := '';
begin
    for i in 0 .. TG_nargs - 1 loop
        if i > 0 then
            argstr := argstr || ', ';
        end if;
        argstr := argstr || TG_argv[i];
    end loop;

    raise notice '% % % % (%)', TG_RELNAME, TG_WHEN, TG_OP, TG_LEVEL, argstr;

    if TG_LEVEL = 'ROW' then
        if TG_OP = 'INSERT' then
            raise NOTICE 'NEW: %', NEW;
            INSERT INTO main_table VALUES (NEW.a, NEW.b);
            RETURN NEW;
        end if;

        if TG_OP = 'UPDATE' then
            raise NOTICE 'OLD: %, NEW: %', OLD, NEW;
            UPDATE main_table SET a = NEW.a, b = NEW.b WHERE a = OLD.a AND b = OLD.b;
            if NOT FOUND then RETURN NULL; end if;
            RETURN NEW;
        end if;

        if TG_OP = 'DELETE' then
            raise NOTICE 'OLD: %', OLD;
            DELETE FROM main_table WHERE a = OLD.a AND b = OLD.b;
            if NOT FOUND then RETURN NULL; end if;
            RETURN OLD;
        end if;
    end if;

    RETURN NULL;
end;
$$;

-- Before row triggers aren't allowed on views
CREATE TRIGGER invalid_trig BEFORE INSERT ON main_view
FOR EACH ROW EXECUTE PROCEDURE trigger_func('before_ins_row');

CREATE TRIGGER invalid_trig BEFORE UPDATE ON main_view
FOR EACH ROW EXECUTE PROCEDURE trigger_func('before_upd_row');

CREATE TRIGGER invalid_trig BEFORE DELETE ON main_view
FOR EACH ROW EXECUTE PROCEDURE trigger_func('before_del_row');

-- After row triggers aren't allowed on views
CREATE TRIGGER invalid_trig AFTER INSERT ON main_view
FOR EACH ROW EXECUTE PROCEDURE trigger_func('before_ins_row');

CREATE TRIGGER invalid_trig AFTER UPDATE ON main_view
FOR EACH ROW EXECUTE PROCEDURE trigger_func('before_upd_row');

CREATE TRIGGER invalid_trig AFTER DELETE ON main_view
FOR EACH ROW EXECUTE PROCEDURE trigger_func('before_del_row');

-- Truncate triggers aren't allowed on views
CREATE TRIGGER invalid_trig BEFORE TRUNCATE ON main_view
EXECUTE PROCEDURE trigger_func('before_tru_row');

CREATE TRIGGER invalid_trig AFTER TRUNCATE ON main_view
EXECUTE PROCEDURE trigger_func('before_tru_row');

-- INSTEAD OF triggers aren't allowed on tables
CREATE TRIGGER invalid_trig INSTEAD OF INSERT ON main_table
FOR EACH ROW EXECUTE PROCEDURE view_trigger('instead_of_ins');

CREATE TRIGGER invalid_trig INSTEAD OF UPDATE ON main_table
FOR EACH ROW EXECUTE PROCEDURE view_trigger('instead_of_upd');

CREATE TRIGGER invalid_trig INSTEAD OF DELETE ON main_table
FOR EACH ROW EXECUTE PROCEDURE view_trigger('instead_of_del');

-- Don't support WHEN clauses with INSTEAD OF triggers
CREATE TRIGGER invalid_trig INSTEAD OF UPDATE ON main_view
FOR EACH ROW WHEN (OLD.a <> NEW.a) EXECUTE PROCEDURE view_trigger('instead_of_upd');

-- Don't support column-level INSTEAD OF triggers
CREATE TRIGGER invalid_trig INSTEAD OF UPDATE OF a ON main_view
FOR EACH ROW EXECUTE PROCEDURE view_trigger('instead_of_upd');

-- Don't support statement-level INSTEAD OF triggers
CREATE TRIGGER invalid_trig INSTEAD OF UPDATE ON main_view
EXECUTE PROCEDURE view_trigger('instead_of_upd');

-- Valid INSTEAD OF triggers
CREATE TRIGGER instead_of_insert_trig INSTEAD OF INSERT ON main_view
FOR EACH ROW EXECUTE PROCEDURE view_trigger('instead_of_ins');

CREATE TRIGGER instead_of_update_trig INSTEAD OF UPDATE ON main_view
FOR EACH ROW EXECUTE PROCEDURE view_trigger('instead_of_upd');

CREATE TRIGGER instead_of_delete_trig INSTEAD OF DELETE ON main_view
FOR EACH ROW EXECUTE PROCEDURE view_trigger('instead_of_del');

-- Valid BEFORE statement VIEW triggers
CREATE TRIGGER before_ins_stmt_trig BEFORE INSERT ON main_view
FOR EACH STATEMENT EXECUTE PROCEDURE view_trigger('before_view_ins_stmt');

CREATE TRIGGER before_upd_stmt_trig BEFORE UPDATE ON main_view
FOR EACH STATEMENT EXECUTE PROCEDURE view_trigger('before_view_upd_stmt');

CREATE TRIGGER before_del_stmt_trig BEFORE DELETE ON main_view
FOR EACH STATEMENT EXECUTE PROCEDURE view_trigger('before_view_del_stmt');

-- Valid AFTER statement VIEW triggers
CREATE TRIGGER after_ins_stmt_trig AFTER INSERT ON main_view
FOR EACH STATEMENT EXECUTE PROCEDURE view_trigger('after_view_ins_stmt');

CREATE TRIGGER after_upd_stmt_trig AFTER UPDATE ON main_view
FOR EACH STATEMENT EXECUTE PROCEDURE view_trigger('after_view_upd_stmt');

CREATE TRIGGER after_del_stmt_trig AFTER DELETE ON main_view
FOR EACH STATEMENT EXECUTE PROCEDURE view_trigger('after_view_del_stmt');

\set QUIET false

-- Insert into view using trigger

INSERT INTO main_view VALUES (20, 30);
INSERT INTO main_view VALUES (21, 31) RETURNING a, b;

-- Table trigger will prevent updates
UPDATE main_view SET b = 31 WHERE a = 20;
UPDATE main_view SET b = 32 WHERE a = 21 AND b = 31 RETURNING a, b;

-- Remove table trigger to allow updates
DROP TRIGGER before_upd_a_row_trig ON main_table;
UPDATE main_view SET b = 31 WHERE a = 20;
UPDATE main_view SET b = 32 WHERE a = 21 AND b = 31 RETURNING a, b;
SELECT * from main_view order by a, b;

-- Before and after stmt triggers should fire even when no rows are affected
UPDATE main_view SET b = 0 WHERE false;

-- Delete from view using trigger
DELETE FROM main_view WHERE a IN (10,20);
DELETE FROM main_view WHERE a = 31 RETURNING a, b;
SELECT * from main_view order by a, b;

\set QUIET true

-- Describe view should list triggers
\o :desc_output_file
\d main_view
\o
:replace_temp_schema_name;

-- Test dropping view triggers
DROP TRIGGER instead_of_insert_trig ON main_view;
DROP TRIGGER instead_of_delete_trig ON main_view;
\o :desc_output_file
\d main_view
\o
:replace_temp_schema_name;

-- Test alter (rename) triggers
ALTER TRIGGER after_ins_stmt_trig ON main_view RENAME TO after_ins_stmt_trig_new_name;
\o :desc_output_file
\d main_view
\o
:replace_temp_schema_name;

DROP VIEW main_view;
DROP TABLE main_table;

-- Test self-referential triggers (on non-temp table).
-- Note: The YugaByte behavior in this case is different from vanilla Postgres.
--  * Vanilla Postgres would throw an error when a row is modified by a before-row trigger
--    and also by the main statement itself.
--  * However, in YugaByte this is allowed and both changes are applied to the row in the
--    expected order (i.e. the before trigger is applied first). This includes the case when
--    the statement (latter) change would override the changes made by the before trigger
-- For example, in the delete example below, the parent's row(s) will be deleted by the main statement
-- after being updated (to decrement nchildren) by the before trigger (of the child row).
create table self_ref_trigger (
    id int primary key,
    parent int references self_ref_trigger,
    data text,
    nchildren int not null default 0
);

create function self_ref_trigger_ins_func()
  returns trigger language plpgsql as
$$
begin
  if new.parent is not null then
    update self_ref_trigger set nchildren = nchildren + 1
      where id = new.parent;
  end if;
  return new;
end;
$$;
create trigger self_ref_trigger_ins_trig before insert on self_ref_trigger
  for each row execute procedure self_ref_trigger_ins_func();

create function self_ref_trigger_del_func()
  returns trigger language plpgsql as
$$
begin
  if old.parent is not null then
    update self_ref_trigger set nchildren = nchildren - 1
      where id = old.parent;
  end if;
  return old;
end;
$$;
create trigger self_ref_trigger_del_trig before delete on self_ref_trigger
  for each row execute procedure self_ref_trigger_del_func();

create index nonconcurrently on self_ref_trigger (parent asc);
create index nonconcurrently on self_ref_trigger (data asc);
create index nonconcurrently on self_ref_trigger (nchildren asc);

insert into self_ref_trigger values (1, null, 'root');
insert into self_ref_trigger values (2, 1, 'root child A');
insert into self_ref_trigger values (3, 1, 'root child B');
insert into self_ref_trigger values (4, 2, 'grandchild 1');
insert into self_ref_trigger values (5, 3, 'grandchild 2');

update self_ref_trigger set data = 'root!' where id = 1;
update self_ref_trigger set nchildren = nchildren + 1;
update self_ref_trigger set nchildren = nchildren - 1;

-- Check that all indexes are consistent with the table (see #20648).
\set ordering ''
\set query 'SELECT * FROM (select * from self_ref_trigger :ordering LIMIT ALL) ybview ORDER BY id'
\set run 'EXPLAIN (costs off) :query; :query'
:run;
\set ordering 'order by parent'
:run;
\set ordering 'order by data'
:run;
\set ordering 'order by nchildren'
:run;

delete from self_ref_trigger where id in (2, 4);

\set ordering ''
:run;

delete from self_ref_trigger;

:run;

drop table self_ref_trigger;
drop function self_ref_trigger_ins_func();
drop function self_ref_trigger_del_func();

CREATE TABLE incremental_key(h INT, r INT, v1 INT, v2 INT, PRIMARY KEY(h, r ASC));

CREATE OR REPLACE FUNCTION increment_key() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  NEW.r = NEW.r + 1;
  RETURN NEW;
END;
$$;

CREATE TRIGGER increment_key_trigger BEFORE UPDATE ON incremental_key
FOR EACH ROW EXECUTE PROCEDURE increment_key();

INSERT INTO incremental_key VALUES(1, 1, 1, 1);
SELECT * FROM incremental_key;
UPDATE incremental_key SET v1 = 10 WHERE h = 1;
SELECT * FROM incremental_key;

UPDATE incremental_key SET v1 = 10 WHERE yb_hash_code(h) = yb_hash_code(1);
SELECT * FROM incremental_key;

DROP TABLE incremental_key;
DROP FUNCTION increment_key;

CREATE TABLE incremental_value(h INT, r INT, v1 INT, v2 INT, PRIMARY KEY(h, r ASC));

CREATE OR REPLACE FUNCTION increment_value() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  NEW.v2 = NEW.v2 + 1;
  RETURN NEW;
END;
$$;

CREATE TRIGGER increment_value_trigger BEFORE UPDATE ON incremental_value
FOR EACH ROW EXECUTE PROCEDURE increment_value();

INSERT INTO incremental_value VALUES(1, 1, 1, 1);
SELECT * FROM incremental_value;
UPDATE incremental_value SET v1 = 10 WHERE h = 1;
SELECT * FROM incremental_value;

DROP TABLE incremental_value;
DROP FUNCTION increment_value;

-- Verify yb_db_admin can alter triggers on tables it does not own
CREATE TABLE foo(a INT);
CREATE TABLE bar(b INT);
CREATE OR REPLACE FUNCTION some_func() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  INSERT INTO bar(b) VALUES (1);
  RETURN NEW;
END;
$$;
SET SESSION AUTHORIZATION yb_db_admin;
-- Alter trigger
CREATE TRIGGER example_trigger AFTER INSERT ON foo FOR EACH ROW EXECUTE PROCEDURE some_func();
ALTER TRIGGER example_trigger ON foo RENAME TO example_trigger_new;
DROP TRIGGER example_trigger_new ON foo;
-- Recreate trigger
CREATE TRIGGER example_trigger AFTER INSERT ON foo FOR EACH ROW EXECUTE PROCEDURE some_func();
RESET SESSION AUTHORIZATION;
-- Verify trigger was run
INSERT INTO foo VALUES (0);
SELECT * from bar;
-- cleanup
DROP TABLE foo;
DROP TABLE bar;

-- Verify yb_db_admin can alter system triggers
CREATE TABLE foo(a INT UNIQUE);
CREATE TABLE bar(b INT);
ALTER TABLE bar ADD CONSTRAINT baz FOREIGN KEY (b) REFERENCES foo(a);
SET SESSION AUTHORIZATION yb_db_admin;
ALTER TABLE bar ENABLE TRIGGER ALL;
ALTER TABLE bar DISABLE TRIGGER ALL;
ALTER TABLE pg_shdepend ENABLE TRIGGER ALL;
-- cleanup
DROP TABLE bar;
DROP TABLE foo;
RESET SESSION AUTHORIZATION
