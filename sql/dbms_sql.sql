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
