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

-- TODO(jason): remove when issue #1721 is closed or closing.
DISCARD TEMP;
