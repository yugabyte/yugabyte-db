do $$ begin for i in 1..5000 loop insert into test values (i, 400, 404); end loop; end; $$;
