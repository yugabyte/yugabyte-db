set timezone = 'Atlantic/Faeroe';
with v as (
  select 
    '2021-01-01 12:00:00 UTC'::timestamptz as t1,
    '2021-07-01 12:00:00 UTC'::timestamptz as t2
  )
select
  to_char(t1, 'hh24:mi:ss TZ TZH:TZM') as t1,
  to_char(t2, 'hh24:mi:ss TZ TZH:TZM') as t2
from v;





