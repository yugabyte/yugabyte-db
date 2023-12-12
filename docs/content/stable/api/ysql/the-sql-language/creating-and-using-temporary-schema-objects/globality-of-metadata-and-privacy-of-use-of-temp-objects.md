---
title: Demonstrate the globality of metadata, and the privacy of use, of temporary objects [YSQL]
headerTitle: Demonstrate the globality of metadata, and the privacy of use, of temporary objects
linkTitle: Globality of metadata and privacy of use of temp objects
description: Demonstrates that all sessions can see the metadata for temporary objects created by all other sessions—but that only the session that created a temporary object can use it. [YSQL]
menu:
  stable:
    identifier: globality-of-metadata-and-privacy-of-use-of-temp-objects
    parent: creating-and-using-temporary-schema-objects
    weight: 300
type: docs
---

These tests will show that temporary object metadata is visible across different sessions but that you can use a temporary object only in the session that created it.

## Demonstrate the globality of metadata

Create and save this SQL script as _"prepare-qry.sql"_.

```plpgsql
prepare qry as
with c(name, is_my_temp, schema, owner) as (
  select
    c.relname,
    c.relnamespace = pg_my_temp_schema(),
    n.nspname,
    r.rolname
  from
    pg_class as c
    inner join
    pg_namespace as n
    on c.relnamespace = n.oid
    inner join
    pg_roles as r
    on c.relowner = r.oid
  where relkind = 'r')
select name, is_my_temp::text, schema
from c
where owner = current_role
order by (replace(schema::text, 'pg_temp_', ''))::int;
```

And create and save this SQL script as _"t.sql"_.

```plpgsql
-- For example, connect as the role "d0$u0" to the database "d0".
\c d0 d0$u0
create table pg_temp.t(k int, v int) on commit delete rows;

\ir prepare-qry.sql

start transaction;
insert into pg_temp.t(k, v) values(1, 42);
select * from pg_temp.t;
execute qry;
```

Start a new session as an ordinary role that has _all_ privileges on the current database. Call this _"Session 0"_. Make sure that there are no other sessions using this database. Then do this:

```plpgsql
-- For example, connect as the role "d0$u0" to the database "d0".
\c d0 d0$u0
\i prepare-qry.sql
execute qry;
```

At this stage, it will produce no rows. Then, in a new terminal window, start a new session as the same test user connecting to the same database. Call this _"Session 1"_.  Execute the script:

```plpgsql
\i t.sql
```

Repeat this for a reasonable number of newly started sessions. Three is enough. But the more you start, the more convincing the demo will be of the rule for the behavior. When you've started as many sessions as you intend to, repeat _execute qry_ in each session.

With _"Session 0"_ through _"Session 3"_, you'll see (something like) this in _"Session 0"_:

```output
 name | is_my_temp |  schema   
------+------------+-----------
 t    | false      | pg_temp_3
 t    | false      | pg_temp_4
 t    | false      | pg_temp_5
```

The actual numbers appended to _pg_temp\__ are unpredictable. Notice that _pg_my_temp_schema()_ returned _false_ for each of the three temporary schemas. The results show that each session that executed this:

```plpgsql
create table pg_temp.t
```

using a schema-qualified identifier for the temporary table that starts with the alias _pg_temp_, has created a differently-named temporary schema. This, of course, must be the case because a schema-object is uniquely identified by its name and the name of the schema where it lives—and the table always has the same name, _t_. You'll then see this in _"Session 2"_:

```output
 name | is_my_temp |  schema   
------+------------+-----------
 t    | true       | pg_temp_3
 t    | false      | pg_temp_4
 t    | false      | pg_temp_5
```

Notice that _pg_my_temp_schema()_ returned _true_ for _pg_temp_3_ and_false_ for the other two temporary schemas. The same pattern continues: _pg_temp_4_ is the temporary schema for _"Session 2"_; and _pg_temp_5_ is the temporary schema for _"Session 3"_.

Now exit each of _"Session 3"_, _"Session 2"_, and _"Session 1"_, in turn, and after exiting each repeat _execute qry_ in _"Session 0"_. You'll see that when each session exits, its temporary schema vanishes.

## Demonstrate the privacy of use of temporary objects

First, create a new version of _"prepare-qry.sql"_ thus:

```plpgsql
prepare qry as
with c(name, kind, is_my_temp, schema, owner) as (
  select
    c.relname,
    'table',
    c.relnamespace = pg_my_temp_schema(),
    n.nspname,
    r.rolname
  from
    pg_class as c
    inner join
    pg_namespace as n
    on c.relnamespace = n.oid
    inner join
    pg_roles as r
    on c.relowner = r.oid
  where relkind = 'r'

  union all

  select
    p.proname,
    'function',
    p.pronamespace = pg_my_temp_schema(),
    n.nspname,
    r.rolname
  from
    pg_proc as p
    inner join
    pg_namespace as n
    on p.pronamespace = n.oid
    inner join
    pg_roles as r
    on p.proowner = r.oid
  where prokind = 'f')
select name, kind, is_my_temp::text, schema
from c
where owner = current_role
order by name desc;
```


Now exit and re-start _"Session 0"_ as, so far, the only session and create and populate a temporary table and create a temporary function to display its contents.

```plpgsql
\c d0 d0$u0
create table pg_temp.t(k int);
insert into pg_temp.t(k) values (17), (42), (57);
create function pg_temp.f(i out int)
  returns setof int
  language sql
  set search_path = pg_catalog, pg_temp
as $body$
  select k from pg_temp.t order by k;
$body$;

select i from pg_temp.f();
```

This is the result:

```output
 i  
----
 17
 42
 57
```

Now, still in the same session, do this:

```plpgsql
\ir prepare-qry.sql
execute qry;
```

This is the result:

```output
 name |   kind   | is_my_temp |  schema   
------+----------+------------+-----------
 t    | table    | true       | pg_temp_2
 f    | function | true       | pg_temp_2
```

To prepare for the second part of the test, demonstrate that it is possible to identify the actual temporary schema that the current session uses rather than do this with the _pg_temp_ alias:

```plpgsql
select k from pg_temp_2.t order by k;
```

and

```plpgsql
select i from pg_temp_2.f(); 
```

Now start a second session and do this:

```plpgsql
\c d0 d0$u0
\ir prepare-qry.sql
execute qry;
```

Just as the tests in the section [Demonstrate the globality of metadata](./#demonstrate-the-globality-of-metadata) led to expect, you'll see this:

```output
 name |   kind   | is_my_temp |  schema   
------+----------+------------+-----------
 t    | table    | false      | pg_temp_2
 f    | function | false      | pg_temp_2
```

In other words, you see the same facts about the same temporary schema-objects with the difference that the temporary schema where they live does not belong to the current session. Now try to use the temporary schema-objects using the same explicit identifiers that worked in the session to which  the _pg_temp_2_ belongs:

```plpgsql
select k from pg_temp_2.t order by k;
```

and

```plpgsql
select i from pg_temp_2.f(); 
```

Each of these attempts fails with the same error:

```output
permission denied for schema pg_temp_2
```

Arguably, the error could have been better worded. After all, the owner of _pg_temp_2_ is the role that implicitly created it. And this same role has authorized _both_ sessions. Nevertheless, the meaning is clear: only the session that created a temporary schema-object can use it.