---
title: include-test
headerTitle: include-test
linkTitle: include-test
description: include-test.
menu:
  latest:
    name: 99. DELETE ME
    identifier: include-test
    weight: 9999
type: page
isTocNested: true
showAsideToc: true
---

**REMOVE THIS PAGE** before merging!

1. Here's what we want:

    ```sql {hl_lines=[1,"7-10"]}
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

    ```sql {hl_lines=[1,"7-10"]}
    {{% includeCode file="code-samples/include.sql" spaces=4 %}}
    ```

    * Test further nesting...

      ```sql {hl_lines=[1,"7-10"]}
      {{% includeCode file="code-samples/include.sql" spaces=6 %}}
      ```

1. The `includeFile` version that infers the code language and does its own code block:

    {{< includeFile file="code-samples/include.sql" hl_options="hl_lines=1 7-10" >}}

    * and it's nested more...

      {{< includeFile file="code-samples/include.sql" hl_options="hl_lines=1 7-10" >}}

1. Last item to check on numbering.
