update pg_proc set provolatile = 'i'
  from pg_namespace
 where pg_namespace.oid = pronamespace and nspname = 'oracle'
   and proname in ('trunc','round','length','btrim','ltrim','rtrim','lpad','rpad');