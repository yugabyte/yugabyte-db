---
title: Semantics of "depends on extension" [YSQL]
headerTitle: The semantics of the "depends on extension" subprogram attribute
linkTitle: «Depends on extension» semantics
description: Describes the semantics of the depends on extension subprogram attribute [YSQL].
menu:
  v2.18:
    identifier: depends-on-extension-semantics
    parent: subprogram-attributes
    weight: 10
type: docs
---

Installing an extension typically brings several subprograms. For example, installing the _tablefunc_ extension brings the _normal_rand()_ function. If, later, you drop the extension, then all the subprograms that it brought are silently dropped as a consequence. You don't need to say _cascade_ to bring this outcome. Occasionally, users enlarge an extension's functionality with their own subprograms. They consider that these, too, are part of the extension and they want, therefore, to have these be silently dropped if the extension is dropped. This is what _"depends on extension"_ achieves.

You can test this easily, if you have a sandbox cluster, by creating a brand-new database and installing, say, the _tablefunc_ extension into it, specifying, for example, _"with schema extensions"_. (You need to do this as a _superuser_.) Then create a test function, say _f()_, and a test procedure, say _p()_ in the same database and schema and use _"alter... depends on extension..."_ to make these "part of" _tablefunc_. Use this query to observe this outcome:

```plpgsql
select
  p.proname::text,
  e.extname
from
  pg_catalog.pg_proc p
  inner join
  pg_catalog.pg_depend d
  on p.oid = d.objid
  inner join
  pg_catalog.pg_extension e
  on d.refobjid = e.oid
where p.pronamespace::regnamespace::text = 'extensions'
order by 1, 2, 3;
```

You'll see this:

```output
   proname   |  extname
-------------+-----------
 connectby   | tablefunc
 connectby   | tablefunc
 connectby   | tablefunc
 connectby   | tablefunc
 crosstab    | tablefunc
 crosstab    | tablefunc
 crosstab    | tablefunc
 crosstab2   | tablefunc
 crosstab3   | tablefunc
 crosstab4   | tablefunc
 f           | tablefunc
 normal_rand | tablefunc
 p           | tablefunc
```

Then do this:

```plpgsql
drop extension tablefunc restrict;
```

the _restrict_ keyword is used to emphasize the pedagogy. Usually, it prevents the attempt to drop an object when the object has dependent objects. But dropping an extension is a special case. And "_alter ... depends on extension..."_ recruits your user-defined subprograms into the special-case regime. You can confirm the outcome by attempting to execute _f()_ or _p()_. You'll get the _42883_ (_undefined_function_) error.

{{< note title="'alter ... depends on extension ...' currently draws a warning." >}}
If you use _"alter ... depends on extension ..."_, then you'll get this warning:

```output
0A000: This statement not supported yet
Please report the issue on https://github.com/YugaByte/yugabyte-db/issues
```

But don't create a new issue. Rather, just look at this:

> [Issue #11523](https://github.com/yugabyte/yugabyte-db/issues/11523) The "alter function/procedure depends on extension" variant causes a spurious warning but produces the expected result

{{< /note >}}
