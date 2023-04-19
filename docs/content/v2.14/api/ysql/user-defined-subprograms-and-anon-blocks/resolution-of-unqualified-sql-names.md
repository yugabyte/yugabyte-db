---
title: Resolution of unqualified names in SQL statements that user-defined subprograms issue [YSQL]
headerTitle: Resolution of unqualified names in SQL statements that user-defined subprograms issue
linkTitle: Resolution of unqualified names
description: Explains how unqualified names, used in SQL statements that user-defined subprograms issue, are resolved [YSQL].
menu:
  v2.14:
    identifier: resolution-of-unqualified-sql-names
    parent: user-defined-subprograms-and-anon-blocks
    weight: 20
type: docs
---

When a subprogram issues a SQL statement that uses an unqualified name, an attempt is made to resolve the name to an actual object in some schema according to the current _search_path_—as given by _current_setting('search_path')_. The rule is the same irrespective of whether the subprogram has `security invoker` or `security definer`:

- The _search_path_ may be simply what the function's owner happens to have at the moment that the function is executed.

- Or it may be a pre-determined attribute of the subprogram that you set explicitly as the _[alterable_fn_and_proc_attribute rule](../../syntax_resources/grammar_diagrams/#alterable-fn-and-proc-attribute)_ specifies.

The first possibility (when the subprogram's _search_path_ attribute is left unset) is a notorious security anti-pattern because there's a risk that a bad actor can contrive to change the current _search_path_ at runtime and subvert the intended meaning of the subprogram. The second choice is therefore preferred.

Notice that the risk is more troublesome for a `security definer` subprogram than it is for a `security invoker` subprogram because the former will be designed to do only carefully restricted operations while acting with privileges that the invoking role will typically not have. In contrast, a `security invoker` subprogram is just a convenience encapsulation for what the invoking role could do directly with top-level SQL. Nevertheless the invoker of any subprogram wants to be able to rely on its correctness. And name capture, even if it happens because of carelessness rather than because of a bad actor's deliberate plan, will bring incorrect behavior.

{{< tip title="See the section 'Writing security definer functions safely' in the PostgreSQL documentation." >}}
[Here is the section](https://www.postgresql.org/docs/11/sql-createfunction.html#id-1.9.3.67.10.2) that this tip's title mentions. It explains the risk that the anti-pattern brings and recommends protecting  against it by setting the intended _search_path_ as an attribute of the subprogram.
{{< /tip >}}

## How are unqualified names in the SQL that a subprogram issues resolved?

The following example relies on tables—all in a single database whose name is insignificant, and all with the same owner whose name is also insignificant. The understanding of name resolution is orthogonal to that of object ownership and privileges—as long as you have the appropriate privileges to access all the objects of interest.

- It creates three schemas, _s1_,  _s2_, and  _s3_.
- It creates three tables, all with the same name, _a_—one in each of  _s1_,  _s2_, and  _s3_.
- It creates one table with a different name, _b_, in just the one schema, _s1_.

To run the example, you need to be able to connect to a sandbox database as an ordinary role. (The code below uses _u1_.)

```plpgsql
drop schema if exists s1 cascade;
drop schema if exists s2 cascade;
drop schema if exists s3 cascade;

create schema s1;
create schema s2;
create schema s3;

create table s1.a(k int primary key);
create table s2.a(k int primary key);
create table s3.a(k int primary key);
create table s1.b(k int primary key);
```

List the _oid_, the _name_, and the _name of the schema_ where it lives for each of the tables.

```plpgsql
with c(name, schema, oid) as (
  select
    c.relname ::text,
    n.nspname ::text,
    c.oid     ::text
  from
    pg_class c
    inner join
    pg_namespace n
    on c.relnamespace = n.oid
  where
    c.relowner::regrole::text = 'u1' and
    c.relname in ('a', 'b') and
    c.relkind = 'r')
select name, schema, oid
from c
order by name, schema;
```

The _oid_ values will change every time that you run this example. Here's a typical result:

```output
 name | schema |  oid
------+--------+-------
 a    | s1     | 16572
 a    | s2     | 16577
 a    | s3     | 16582
 b    | s1     | 16587
```

Now create three functions, one in each of the three schemas:

```plpgsql
create function s1.f()
  returns text
  language plpgsql
  set search_path = s1, s2, s3
as $body$
begin
  return
    'a: '||(select 'a'::regclass::oid::text)||' / '||
    'b: '||(select 'b'::regclass::oid::text);
end;
$body$;

create function s2.f()
  returns text
  language plpgsql
  set search_path = s2, s3, s1
as $body$
begin
  return
    'a: '||(select 'a'::regclass::oid::text)||' / '||
    'b: '||(select 'b'::regclass::oid::text);
end;
$body$;

create function s3.f()
  returns text
  language plpgsql
  set search_path = s3, s1, s2
as $body$
begin
  return
    'a: '||(select 'a'::regclass::oid::text)||' / '||
    'b: '||(select 'b'::regclass::oid::text);
end;
$body$;
```

The SQL texts of these three `create` statements are close to identical.

The expressions _'a'::regclass::oid::text_, and correspondingly for _b_, discover the _oid_ values of the actual objects to which the unqualified table names _a_ and _b_ resolve.

The functions differ only in these ways:

- The fully qualified names are, respectively, _s1.f()_, _s2.f()_, and _s3.f()_.
- The _search_paths_ are function-specific, thus:

  ```output
  s1.f(): s1, s2, s3
  s2.f(): s2, s3, s1
  s3.f(): s3, s1, s2
  ```

Execute the functions like this:

```plpgsql
select
  (select s1.f()) as "s1.f()",
  (select s2.f()) as "s2.f()",
  (select s3.f()) as "s3.f()";
```

If you do this immediately after creating all the objects, then you'll see the same _oid_ values again, thus:

```output
       s1.f()        |       s2.f()        |       s3.f()
---------------------+---------------------+---------------------
 a: 16572 / b: 16587 | a: 16577 / b: 16587 | a: 16582 / b: 16587
```

You can see from the _oid_ values that each of the three functions found a different resolution for the unqualified table name _a_; and each found the same resolution for the unqualified table name _b_. But the rule is the same in all cases. A subprogram resolves an unqualified name in the schema that is first on the reigning _search_path_ where an object with the to-be-found name exists.

## Consider using fully qualified names

There are doubtless plausible use cases where it isn't known until run-time in which schema the object that's the intended name resolution target will be found, where it might be found in more than one schema, and where the requirement is to resolve to the one that is first on the _search_path_. But such scenarios are rare.

The conventional use-case for `security definer` subprograms is to provide exactly and only the planned access to table data that the invoking role is unable to operate on directly. This implies a designed regime of several roles where each owns objects in different schemas and where all of the names are given explicitly in the application's design specification. In this case, the dynamic resolution, that uses a _search_path_, is no advantage and also brings a risk.

Relying on the _search_path_ for name resolution (for example, as a consequence of careless programming where the author forgot to use a fully qualified object name but where the intended object is still found) brings another risk:

- Even when _current_setting('search_path')_ doesn't show the _pg_catalog_ and the _pg_temp_ schemas, these are inevitably used by the implementation of name resolution. By default, the _pg_temp_ schema is the very first one to be searched; and the _pg_catalog_ schema is the very next.

This is explained in the PostgreSQL documentation in the subsection _[search_path](https://www.postgresql.org/docs/11/runtime-config-client.html#id-1.6.6.14.2.2.2.1.3)_ in the section [Client Connection Defaults](https://www.postgresql.org/docs/11/runtime-config-client.html). Notice that it explains that _pg_temp_ is an alias for whatever _pg_temp_nnn_ the session happens currently to use.

The _search_path_ account goes on to explain that you can change where _pg_temp_ and _pg_catalog_ come in the search order by mentioning these explicitly when you set the _search_path_. Try this:

```plpgsql
-- Rely, at first, on the default serach order.
select count(*) from pg_class;
```

A typical result in a freshly created database is a few hundred. Now capture the name of this catalog table with a temporary table and repeat the _count(*)_ query:

```plpgsql
create temporary table pg_class(k int);
select count(*) from pg_class;
```

The _count(*)_ result is now _zero_ because the new temporary table has captured the resolution from the same-named table in _pg_catalog_. Now reverse the search order of _pg_temp_ and _pg_catalog_ by setting this explicitly, and repeat the _count(*)_ query again:

```plpgsql
set search_path = pg_catalog, pg_temp;
select count(*) from pg_class;
```

Now the _count(*)_ query will notice one more object in the catalog than it did at the start because of the new presence of the temporary table. The opinion among experts on the _[pgsql-general](mailto:pgsql-general@lists.postgresql.org)_ email list is that the default behavior, to search _pg_temp_ before _pg_catalog_, is unfortunate; and they recommend reversing this—at least in a subprogram's specific setting.

This gives you a convenient way to enforce the practice that a subprogram uses only fully qualified names. Here's an example:

```plpgsql
drop function s1.f();
create function s1.f()
  returns text
  language plpgsql
  set search_path = pg_catalog, pg_temp
as $body$
begin
  return
    'a: '||(select 's1.a'::regclass::oid::text)||' / '||
    'b: '||(select 's1.b'::regclass::oid::text);
end;
$body$;

drop function s2.f();
create function s2.f()
  returns text
  language plpgsql
  set search_path = pg_catalog, pg_temp
as $body$
begin
  return
    'a: '||(select 's2.a'::regclass::oid::text)||' / '||
    'b: '||(select 's1.b'::regclass::oid::text);
end;
$body$;

drop function s3.f();
create function s3.f()
  returns text
  language plpgsql
  set search_path = pg_catalog, pg_temp
as $body$
begin
  return
    'a: '||(select 's3.a'::regclass::oid::text)||' / '||
    'b: '||(select 's1.b'::regclass::oid::text);
end;
$body$;

select
  (select s1.f()) as "s1.f()",
  (select s2.f()) as "s2.f()",
  (select s3.f()) as "s3.f()";
```

The _oid_ values that you see are identical to those that you saw when the subprograms had explicit _search_path_ attributes and used unqualified table names:

```output
       s1.f()        |       s2.f()        |       s3.f()
---------------------+---------------------+---------------------
 a: 16572 / b: 16587 | a: 16577 / b: 16587 | a: 16582 / b: 16587
```

{{< tip title="Use fully qualified names in the SQL that subprograms issue unless you can explain why this is unsuitable." >}}

Yugabyte recommends that, for the typical case, you use fully qualified names in the SQL statements that subprograms issue and that, to reinforce your plan,  you set the _search_path_ to just _pg_catalog_, _pg_temp_ for every subprogram.
{{< /tip >}}

There's no risk that you might create objects in the _pg_catalog_ schema. Connect as the _postgres_ role and try this:

```postgres
set search_path = pg_catalog, pg_temp;
create table pg_catalog.t(n int);
```

It causes this error:

```output
42501: permission denied to create "pg_catalog.t"
```

Notice that it _is_ possible, by setting a special configuration parameter, to allow the _postgres_ role to create objects in the _pg_catalog_ schema. But even then, only the _postgres_ role can create objects there. If a bad actor can manage to connect as the _postgres_ role, then all bets are anyway already off.