create or replace function gen_file() returns void as $$
declare 
  f utl_file.file_type;
  r record;
begin
  f := utl_file.fopen('/tmp','regress_orafce','w');
  for r in select m from generate_series(1,20) m(m) loop
    perform utl_file.put_line(f, r.m::numeric);
  end loop;
  f := utl_file.fclose(f);
end;
$$ language plpgsql;
select gen_file();


create or replace function read_file() returns void as $$
declare 
  f utl_file.file_type;
begin
  f := utl_file.fopen('/tmp','regress_orafce','r');
  loop 
    raise notice '>>%<<', utl_file.get_line(f);
  end loop;
  exception
    -- when no_data_found then,  8.1 plpgsql doesn't know no_data_found
    when others then
      raise notice 'finish % ', sqlerrm;
      raise notice 'kuku';
      f := utl_file.fclose(f);
      raise notice 'bbbb';
end;
$$ language plpgsql;
select read_file();
