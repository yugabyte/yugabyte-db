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

Here's what we want:

```sql
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

Here's the `includeCode` version that gets wrapped in a fenced code block:

```sql
{{% includeCode "code-samples/include.sql" %}}
```

And here's the `includeFile` version that infers the code language and does its own code block:

{{% includeFile "code-samples/include.sql" %}}

And here's an attempt to call a partial template:

{{% partial included_code "code-sample/include.sql" %}}
