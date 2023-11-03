---
title: Temporary tables, views, sequences, and indexes [YSQL]
headerTitle: Temporary tables, views, sequences, and indexes
linkTitle: Temp tables, views, sequences, and indexes
description: Describes the dedicated syntax for creating temporary tables, views, and sequences. Shows that the index on a temporary table is necessarily temporary. [YSQL]
menu:
  preview:
    identifier: temporary-tables-views-sequences-and-indexes
    parent: creating-and-using-temporary-schema-objects
    weight: 100
type: docs
---

You can create a temporary _table_, a temporary _view_, or a temporary _sequence_ by using dedicated syntax.

{{< note title="There's no dedicated syntax to create a temporary index." >}}
The _[create index](../../statements/ddl_create_index/)_ statement does not accept a schema-qualified identifier for the to-be-created index. This reflects the fact that an index is always, non-negotiably, in the same schema as the table upon whose column(s) it's created. As an extension of this, an index that's created on a temporary table's column(s) will be in the session's temporary schema—in other words, it will be a temporary index.
{{< /note >}}

Start a new session as an ordinary role that has _all_ privileges on the current database. Try this:

```plpgsql
-- For example, connect as the role "d0$u0" to the database "d0".
\c d0 d0$u0

create temporary table    t(k int, v int) on commit delete rows;
create temporary view     v as select k, v from t where k > 0;
create temporary sequence s as int start with 10 increment by 5;
create           index    i on pg_temp.t(k);
```

Check the outcome thus:

```plpgsql
prepare qry as
with c(name, kind, is_my_temp, schema, owner) as (
  select
    c.relname,
    case c.relkind
      when 'r' then 'table'
      when 'v' then 'view'
      when 'S' then 'sequence'
      when 'i' then 'index'
      else          'other'
    end,
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
    on c.relowner = r.oid)
select name, kind, is_my_temp::text, schema
from c
where owner = current_role
order by (replace(schema::text, 'pg_temp_', ''))::int;
execute qry;
```

This is the result:

```output
 name |   kind   | is_my_temp |  schema   
------+----------+------------+-----------
 t    | table    | true       | pg_temp_2
 v    | view     | true       | pg_temp_2
 s    | sequence | true       | pg_temp_2
 i    | index    | true       | pg_temp_2
```
Notice that the _pg_class_ catalog table has no column that denotes the status of the listed schema-objects as temporary or permanent. Rather, you infer this status from the schema where the object lives. The _pg_my_temp_schema()_ builtin-function (see the PostgreSQL documentation section [System Information Functions](https://www.postgresql.org/docs/11/functions-info.html)) returns the _oid_ of the current session's temporary schema, or zero if it has no temporary schema. Temporary schema names start with _pg_temp\__ and then one or a few digits. You might see something other than _"2"_ when you try this test.

When you use the _temporary_ keyword, you cannot denote the to-be-created  schema-object with a qualified identifier because its temporary status implies that it will live in the current session's temporary schema. Such schemas are not created as a side effect of creating a database. And nor is there syntax to let you create a temporary schema later. Rather, it's created as a side-effect of creating a session's first temporary schema-object. Moreover, you  cannot predict what digits will be appended to _pg_temp\__ to form its name. However, you can use the alias _pg_temp_ in a schema-qualified identifier to denote whatever temporary schema you end up with. This gives you a clue to an alternative syntax to create temporary tables, views, and synonyms. Try this:

```plpgsql
drop table    if exists pg_temp.t cascade;
drop view     if exists pg_temp.v cascade;
drop sequence if exists pg_temp.s cascade;

create table    pg_temp.t(k int, v int) on commit delete rows;
create view     pg_temp.v as select k, v from t where k > 0;
create sequence pg_temp.s as int start with 10 increment by 5;

create index            i on pg_temp.t(k);

execute qry;
```

The result of _execute qry_ is exactly the same as when you created the temporary schema-objects with the syntax that uses the _temporary_ keyword.
