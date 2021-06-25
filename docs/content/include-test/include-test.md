---
title: include-test
headerTitle: include-test
linkTitle: include-test
description: include-test.
menu:
  latest:
    identifier: include-test
    parent: name-res-rules
    weight: 99
isTocNested: true
showAsideToc: true
---

1. Here's what we want:

    ```sql {hl_lines=[2,"5-7"]}
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
    ```

1. Call `includeCode` within a fenced code block:

    ```sql
    {{% includeCode "code-samples/include.sql" %}}
    ```

1. The `includeFile` version that infers the code language and does its own code block:

    {{% includeFile "code-samples/include.sql" sql "hl_lines=2 5-7" %}}

1. Call `highlight` around inline text:

    {{< highlight sql "hl_lines=2 5-7" >}}
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
{{< /highlight >}}

1. Call `highlight` around `includeCode`:

    {{< highlight sql "hl_lines=2 5-7" >}}{{% includeCode "code-samples/include.sql" %}}{{< /highlight >}}
