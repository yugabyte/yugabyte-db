---
title: Creating temporary schema-objects of all kinds [YSQL]
headerTitle: Creating temporary schema-objects of all kinds
linkTitle: Temp schema-objects of all kinds
description: Describes how to create temporary schema-objects of all kinds by using a schema-qualified identifier for the target object that starts with the alias 'pg_temp' [YSQL]
menu:
  v2.18:
    identifier: creating-temporary-schema-objects-of-all-kinds
    parent: creating-and-using-temporary-schema-objects
    weight: 200
type: docs
---

You can create temporary schema-objects of all kinds, like composite types, functions, and operators. However, for historical reasons, there is no dedicated syntax that starts with _create temporary_ except for tables, views, and sequences. Rather, you use the general approach that was shown in the section [Temporary tables, views, sequences, and indexes](../temporary-tables-views-sequences-and-indexes/) that simply uses an identifier for the to-be-created temporary object that's qualified using the _pg_temp_ alias. This ability is not called out specifically in the PostgreSQL documentation. But [this turn](https://www.postgresql.org/message-id/15191.1208975632@sss.pgh.pa.us) in the thread _"Create temporary function"_ on the _pgsql-general_ email list, sent by Tom Lane (an authority on PostgreSQL's implementation), confirms the supported status of the ability.

Here are some examples. But first, look at the section [Name resolution within top-level SQL statements](../../../name-resolution-in-top-level-sql/). It recommends that you always mention _pg_temp_ explicitly in the _search_path_ and that you place it rightmost so that it is searched last. This implies that it's prudent always to refer to a temporary schema-object with a schema-qualified identifier that starts with _pg_temp_.

{{< note title="An unqualified identifier never resolves to a temporary procedure, a temporary function, or a temporary operator." >}}
You must use a qualified identifier that starts with _pg_temp_ to refer to a temporary procedure, a temporary function, or a temporary operator. The PostgreSQL designers think that, without this feature of the implementation, and when _pg_temp_ is leftmost in the _search_path_ (as is the default), the behavior of application code that referred to a permanent procedure, a permanent function, or a permanent operator could be subverted by capturing it name with a temporary schema-object of the same kind and with the same signature.

Yugabyte recommends that you insure yourself against name capture not only by temporary objects but also by permanent objects by _always_ referring to schema-objects in application code with schema-qualified identifiers.
{{< /note >}}

Start a new session as an ordinary role that has _all_ privileges on the current database.

Then run each of the following examples. When you're done, look at the metadata for all of them.

## Create a temporary view

The _pg_temp.my_temporary_schema_objects_ temporary view lists all temporary schema-objects, of all kinds, that are owned by the _current_role_. As it happens, it doesn't need to be parameterized. But the need for a parameterizable temporary view would be met by encapsulating the _select_ statement within a temporary _language sql_ function with appropriate _out_ parameters as well as the )_in_ parameters that determine the results. Notice the functional equivalence between such a temporary function and a prepared SQL statement.

```plpgsql
-- For example, connect as the role "d0$u0" to the database "d0".
\c d0 d0$u0

create view pg_temp.my_temporary_schema_objects(name, kind) as
  with o(name, kind, schema_oid, owner_oid) as
    (
      select
        relname,
        case relkind
          when 'r' then 'table'
          when 'v' then 'view'
          when 'i' then 'index'
          when 'S' then 'sequence'
          when 'c' then 'composite-type'
          when 't' then 'TOAST table'
          else          'other'
        end,
        relnamespace,
        relowner
      from pg_class
      -- Filter out the row that's automatically generated as the partner
      -- to a manually created composite type.
      where relkind <> 'c'

    union all
      select
        t.typname,
        'composite-type',
        t.typnamespace,
        t.typowner
      from
        pg_type as t
        inner join
        pg_class as c
        on t.typname = c.relname and t.typnamespace = c.relnamespace
      where t.typtype ='c'
      -- Filter out the row that's automatically generated as the partner
      -- to a manually created table or manually created view - or even a sequence!.
      and not (c.relkind = 'r' or c.relkind = 'v' or c.relkind = 'S')

    union all
      select
        typname,
        case typtype
          when 'd' then 'domain'
          when 'e' then 'enum'
          else          'other'
        end,
        typnamespace,
        typowner
      from pg_type t
      where (typtype = 'd' or typtype = 'e')

    union all
      select
        proname,
        case prokind
          when 'f' then 'function'
          when 'p' then 'procedure'
          when 'a' then 'aggregate'
          when 'w' then 'window'
        end,
        pronamespace,
        proowner
      from pg_proc

    union all
      select
        o.oprname,
        'operator',
        o.oprnamespace,
        o.oprowner
      from
        pg_operator as o
        inner join
        pg_proc as p
        on o.oprcode = p.oid
    )
select
  o.name,
  o.kind
 from
  o
  inner join
  pg_roles as r
  on o.owner_oid = r.oid
where r.rolname = current_role
and o.schema_oid = pg_my_temp_schema();
```

You can now simply query this view at any moment during the rest of the session's lifetime to see the temporary schema-objects that you have created to date.

## Create a temporary composite type

```plpgsql
create type pg_temp.cmplx_no as (real_cpt numeric, imagry_cpt numeric);
```
## Create a temporary enum

```plpgsql
create type pg_temp.colors as enum (
  'red', 'yellow', 'green', 'blue', 'indigo', 'violet');
```

##  Create a temporary function and temporary domain

```plpgsql
create function pg_temp.is_positive(i in int)
  returns boolean
  language sql
as $body$
  select i > 0;
$body$;

create domain pg_temp.natural as int
constraint natural_ok check(pg_temp.is_positive(value));
```

##  Create a temporary function and temporary operator

```plpgsql
create function pg_temp.cmplx_equals(
  a1 in pg_temp.cmplx_no,
  a2 in pg_temp.cmplx_no)
  returns boolean
  language sql
as $body$
  select (a1.real_cpt   = a2.real_cpt) and
         (a1.imagry_cpt = a2.imagry_cpt);
$body$;

create operator pg_temp.= (
  leftarg   = pg_temp.cmplx_no,
  rightarg  = pg_temp.cmplx_no,
  procedure = pg_temp.cmplx_equals);

select (
         (17, 42)::pg_temp.cmplx_no
         operator(pg_temp.=)
         (42, 17)::pg_temp.cmplx_no
       )::text;
```

This works, and produces the expected result: _false_. But notice the baroque syntax that you need to use the temporary equality operator. This reflects the fact that, as mentioned, an unqualified identifier is never resolved to a temporary operator.

## Create a temporary table, a temporary index, a temporary sequence, and a temporary procedure

```plpgsql
create table pg_temp.tab(k int, v int);
create index idx on pg_temp.tab(k);
create sequence pg_temp.tab_seq as int start with 10 increment by 5;

create procedure pg_temp.insert_tab(new_v in int)
  language plpgsql
  security definer
  set search_path = pg_catalog, pg_temp
as $body$
declare
  new_k constant int not null := (select nextval('tab_seq'));
begin
  insert into tab(k, v) values (new_k, new_v);
end;
$body$;

call pg_temp.insert_tab(42);
select * from pg_temp.tab;
```

This works—and produces this result:

```output
 k  | v  
----+----
 10 | 42
```

But it exhibits very questionable style. Yugabyte recommends that you should routinely refer to every temporary schema-object, in all contexts, by a qualified identifier that starts with _pg_temp_. It recommends against reasoning (to the extent that you can depend on your reasoning) to establish when you can safely use an unqualified identifier for a temporary schema-object.

## List the set of temporary schema-objects that have been created to date

```plpgsql
select name, kind from pg_temp.my_temporary_schema_objects order by name;
```

This is the result:

```output
            name             |      kind      
-----------------------------+----------------
 =                           | operator
 cmplx_equals                | function
 cmplx_no                    | composite-type
 colors                      | enum
 idx                         | index
 insert_tab                  | procedure
 is_positive                 | function
 my_temporary_schema_objects | view
 natural                     | domain
 tab                         | table
 tab_seq                     | sequence
```
