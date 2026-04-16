update pg_proc set provolatile = 'i'
  from pg_namespace
 where pg_namespace.oid = pronamespace and nspname = 'oracle'
   and proname in ('trunc','round','length','btrim','ltrim','rtrim','lpad','rpad');

update pg_type set typcollation = 100
 where typname in ('varchar2', 'nvarchar2');
