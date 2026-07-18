\set ECHO none

-- YB note: advisory locks not yet supported.
-- wait for other processes, wait max 100 sec
do $$
declare c int;
begin
  if pg_try_advisory_xact_lock(1) then
    for i in 1..1000 loop
      perform pg_sleep(0.1);
      c := (select count(*) from pg_locks where locktype = 'advisory' and objid = 1 and not granted);
      if c = 2 then
        return;
      end if;
    end loop;
  else
    perform pg_advisory_xact_lock(1);
  end if;
end;
$$;

\set ECHO all

/* Register alerts */
SELECT dbms_alert.register('a1');
SELECT dbms_alert.register('a2');

/* Test: multisession waitone */
SELECT dbms_alert.waitone('a1',20);

/* Test: multisession waitany */
SELECT dbms_alert.waitany(10);

/* cleanup */
SELECT dbms_alert.removeall();

