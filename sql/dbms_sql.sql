do $$
declare
  c int;
  strval varchar;
  intval int;
  nrows int default 30;
begin
  c := dbms_sql.open_cursor();
  call dbms_sql.parse(c, 'select ''ahoj'' || i, i from generate_series(1, :nrows) g(i)');
  call dbms_sql.bind_variable(c, 'nrows', nrows);
  call dbms_sql.define_column(c, 1, strval);
  call dbms_sql.define_column(c, 2, intval);
  perform dbms_sql.execute(c);
  while dbms_sql.fetch_rows(c) > 0
  loop
    call dbms_sql.column_value(c, 1, strval);
    call dbms_sql.column_value(c, 2, intval);
    raise notice 'c1: %, c2: %', strval, intval;
  end loop;
  call dbms_sql.close_cursor(c);
end;
$$;

do $$
declare
  c int;
  strval varchar;
  intval int;
  nrows int default 30;
begin
  c := dbms_sql.open_cursor();
  call dbms_sql.parse(c, 'select ''ahoj'' || i, i from generate_series(1, :nrows) g(i)');
  call dbms_sql.bind_variable(c, 'nrows', nrows);
  call dbms_sql.define_column(c, 1, strval);
  call dbms_sql.define_column(c, 2, intval);
  perform dbms_sql.execute(c);
  while dbms_sql.fetch_rows(c) > 0
  loop
    strval := dbms_sql.column_value_f(c, 1, strval);
    intval := dbms_sql.column_value_f(c, 2, intval);
    raise notice 'c1: %, c2: %', strval, intval;
  end loop;
  call dbms_sql.close_cursor(c);
end;
$$;

drop table if exists foo;

create table foo(a int, b varchar, c numeric);

do $$
declare c int;
begin
  c := dbms_sql.open_cursor();
  call dbms_sql.parse(c, 'insert into foo values(:a, :b, :c)');
  for i in 1..100
  loop
    call dbms_sql.bind_variable(c, 'a', i);
    call dbms_sql.bind_variable(c, 'b', 'Ahoj ' || i);
    call dbms_sql.bind_variable(c, 'c', i + 0.033);
    perform dbms_sql.execute(c);
  end loop;
end;
$$;

select * from foo;
truncate foo;

do $$
declare c int;
begin
  c := dbms_sql.open_cursor();
  call dbms_sql.parse(c, 'insert into foo values(:a, :b, :c)');
  for i in 1..100
  loop
    perform dbms_sql.bind_variable_f(c, 'a', i);
    perform dbms_sql.bind_variable_f(c, 'b', 'Ahoj ' || i);
    perform dbms_sql.bind_variable_f(c, 'c', i + 0.033);
    perform dbms_sql.execute(c);
  end loop;
end;
$$;

select * from foo;
truncate foo;

do $$
declare
  c int;
  a int[];
  b varchar[];
  ca numeric[];
begin
  c := dbms_sql.open_cursor();
  call dbms_sql.parse(c, 'insert into foo values(:a, :b, :c)');
  a := ARRAY[1, 2, 3, 4, 5];
  b := ARRAY['Ahoj', 'Nazdar', 'Bazar'];
  ca := ARRAY[3.14, 2.22, 3.8, 4];

  call dbms_sql.bind_array(c, 'a', a);
  call dbms_sql.bind_array(c, 'b', b);
  call dbms_sql.bind_array(c, 'c', ca);
  raise notice 'inserted rows %d', dbms_sql.execute(c);
end;
$$;

select * from foo;
truncate foo;

-- should not to crash, when bound array is null
do $$
declare
  c int;
  ca numeric[];
begin
  c := dbms_sql.open_cursor();
  call dbms_sql.parse(c, 'insert into foo values(:a, 10, 20)');

  call dbms_sql.bind_array(c, 'a', ca);
  raise notice 'inserted rows %d', dbms_sql.execute(c);
end;
$$;

do $$
declare
  c int;
  a int[];
  b varchar[];
  ca numeric[];
begin
  c := dbms_sql.open_cursor();
  call dbms_sql.parse(c, 'insert into foo values(:a, :b, :c)');
  a := ARRAY[1, 2, 3, 4, 5];
  b := ARRAY['Ahoj', 'Nazdar', 'Bazar'];
  ca := ARRAY[3.14, 2.22, 3.8, 4];

  call dbms_sql.bind_array(c, 'a', a, 2, 3);
  call dbms_sql.bind_array(c, 'b', b, 3, 4);
  call dbms_sql.bind_array(c, 'c', ca);
  raise notice 'inserted rows %d', dbms_sql.execute(c);
end;
$$;

select * from foo;
truncate foo;

do $$
declare
  c int;
  a int[];
  b varchar[];
  ca numeric[];
begin
  c := dbms_sql.open_cursor();
  call dbms_sql.parse(c, 'select i, ''Ahoj'' || i, i + 0.003 from generate_series(1, 35) g(i)');
  call dbms_sql.define_array(c, 1, a, 10, 1);
  call dbms_sql.define_array(c, 2, b, 10, 1);
  call dbms_sql.define_array(c, 3, ca, 10, 1);

  perform dbms_sql.execute(c);
  while dbms_sql.fetch_rows(c) > 0
  loop
    call dbms_sql.column_value(c, 1, a);
    call dbms_sql.column_value(c, 2, b);
    call dbms_sql.column_value(c, 3, ca);
    raise notice 'a = %', a;
    raise notice 'b = %', b;
    raise notice 'c = %', ca;
  end loop;
  call dbms_sql.close_cursor(c);
end;
$$;

drop table foo;

create table tab1(c1 integer,  c2 numeric);

create or replace procedure single_Row_insert(c1 integer, c2 numeric)
as $$
declare
  c integer;
  n integer;
begin
  c := dbms_sql.open_cursor();

  call dbms_sql.parse(c, 'INSERT INTO tab1 VALUES (:bnd1, :bnd2)');

  call dbms_sql.bind_variable(c, 'bnd1', c1);
  call dbms_sql.bind_variable(c, 'bnd2', c2);

  n := dbms_sql.execute(c);

  call dbms_sql.debug_cursor(c);
  call dbms_sql.close_cursor(c);
end
$$language plpgsql;

do $$
declare a numeric(7,2);
begin
  call single_Row_insert(2,a);
end
$$;

select * from tab1;

do $$
declare a numeric(7,2) default 1.23;
begin
  call single_Row_insert(2,a);
end
$$;

select * from tab1;
select * from tab1 where c2 is null;


do $$
declare a numeric(7,2);
begin
  call single_Row_insert(0,a);   -- single_Row_insert(0, null)
end
$$;

select * from tab1;

do $$
declare a numeric(7,2) default 1.23;
begin
  call single_Row_insert(0,a);  -- single_Row_insert(0, 1.23)
end
$$;

select * from tab1;
drop procedure single_Row_insert;
drop table tab1;
