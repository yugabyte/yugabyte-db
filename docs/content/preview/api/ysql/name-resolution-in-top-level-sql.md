---
title: Name resolution in top-level SQL [YSQL]
headerTitle: Name resolution within top-level SQL statements
linkTitle: Name resolution in top-level SQL
description: Explains how unqualified identifiers, used within top-level SQL statements, are resolved [YSQL].
menu:
  preview:
    identifier: name-resolution-in-top-level-sql
    parent: api-ysql
    weight: 40
type: docs
---

{{< tip title="Regard the PostgreSQL documentation as the canonical definitional reference for SQL syntax." >}}
In particular, see the section [SQL Syntax](https://www.postgresql.org/docs/11/sql-syntax.html) within the enclosing section [The SQL Language](https://www.postgresql.org/docs/11/sql.html). The discussion of name resolution in top-level SQL statements rests on the notions that the [SQL Syntax](https://www.postgresql.org/docs/11/sql-syntax.html) section explains.
{{< /tip >}}

This section deals mainly with name resolution for the schema-objects that a SQL statement references and how this depends critically on the _search_path_. Name resolution for secondary objects, like columns in relations, is sketched briefly, at the end.

A top-level SQL DML statement uses identifiers to denote any number of schema-objects with kinds like _table_, _view_, _function_, and so on. (The _call_ statement is a special case: it must use an identifier to specify exactly one schema-object with the kind _procedure_.) Further, a _select_, _insert_, _update_, or _delete_ statement typically specifies other secondary phenomena like columns within relations, aliases for relations or columns, the identifiers for common table expressions, or subprogram arguments. Further, a column identifier is sometimes decorated with the typecast operator—and in this case the target type (itself a schema-object) is denoted by an identifier too. And so it goes on. The discussion is easily extended to other kinds of SQL statement like _create_, _alter_, or _drop_ statements.

Each of the identifiers that a SQL statement uses may be unqualified or qualified with the identifier for a schema. And when an unqualified identifier is used, it must be resolved to what a qualified identifier would denote. The resolution scope for schema-objects is the entire current database. And the resolution scope for the secondary phenomena that the statement identifies is established by properties of the overall set of schema-objects that the statement refers to, and by aliases that the statement defines.

## Name resolution for schema-objects

Unqualified identifiers for schema-objects are resolved according to what the _search_path_ specifies.

### The "search_path" run-time parameter

{{< tip title="Treat the PostgreSQL documentation as the canonical reference for 'search_path'." >}}
The PostgreSQL documentation describes the semantics of the _search_path_ run-time parameter in the section [Client Connection Defaults](https://www.postgresql.org/docs/11/runtime-config-client.html#RUNTIME-CONFIG-CLIENT-STATEMENT). Notice this:

> ...the temporary schema is only searched for relation (table, view, sequence, etc) and data type names. _It is never searched for function or operator names_.
{{< /tip >}}

The value of the _search_path_ run-time parameter determines how the names that unqualified identifiers that denote schema-objects are resolved. Briefly, an unqualified identifier denotes only a name and provides no information to specify the schema in which the object with the denoted name is to be found.

{{< tip title="Names and identifiers" >}}
The terms of art _name_ and _identifier_ have different meanings. The difference is explained in the section [Names and identifiers](../names-and-identifiers/).
{{< /tip >}}

The value of _search_path_ is, in general, a comma-separated list of schema identifiers. (The list may specify just one schema; or it may be empty.) You can observe the current value of _search_path_ like this:

```plpgsql
select current_setting('search_path');
```

(The term of art _setting_ is used as a convenient shorthand for _run-time parameter_.)

The _search_path_ setting is always defined—that is it is never _null_. If you create a new session and query it, then—as long as you haven't set its default value as a property of the current database or the current role, you'll see this:

```output
"$user", public
```

This is a reminder that a freshly-created database (unless you have customized the template that _create database_ used) has a schema called _public_ with _all_ privileges granted to the _public_ role.

{{< tip title="Always drop the 'public' schema in a database that will be used in a production application." >}}
The existence of the _public_ schema, where any role can create objects, is convenient in a sandbox database that will be used exclusively for _ad hoc_ experiments with basic syntax and the like. But its presence brings a huge risk in any serious use case. The _set search_path_ statement cannot be restricted. So an unscrupulous user can put the _public_ schema at the start of the search path and create objects to capture the application's intended objects—unless care has been taken always to use schema-qualified identifiers both in SQL statements that client-side code issues and in user-defined subprograms in the database. (User-defined subprograms support an alternative, and recommended, mechanism to control name resolution: you can set the _search_path_ as a subprogram attribute—described [here](../user-defined-subprograms-and-anon-blocks/subprogram-attributes/#alterable-subprogram-attributes).)
{{< /tip >}}

### Show that name resolution always searches 'pg_catalog'

Start a session by authorizing as an ordinary role to a sandbox database on which this role has _all_ privileges and show that _pg_catalog_ is inevitably on the search path:

```plpgsql
create schema "my schema";
set search_path = "my schema";
create table "my schema".xyz_table(k int primary key);

create table "my schema".pg_class(relname name primary key, relkind "char");
insert into "my schema".pg_class(relname, relkind) values('xyz in "my schema".pg_class', 'r');

prepare qry as
select
  (
    select current_setting('search_path') as "search_path"
  ),
  (
    select relname as "row from pg_class"
    from pg_class where relname::text ~ '^xyz'
    and relkind::text = 'r'
  );
execute qry;
```

This is the result:

```output
 search_path | row from pg_class 
-------------+-------------------
 "my schema" | xyz_table
```

Notice that the user-created _pg_class_ table has a row that says where it lives. You can therefore see that the unqualified identifier in the _from_ clause, _from pg_class_, is resolved _not_ to the user-created table in _my schema_ but, rather, to the table with that name in the _pg_catalog_ schema.

{{< tip title="It's bad practice to create a user-object with a name that collides with that of an object in 'pg_catalog'.">}}
See the section [Practice recommendation](./#practice-recommendation) below. That recommendation is ignored in this example in order convincingly to demonstrate how _pg_catalog_ is inevitably included in the effective _search_path_ whether or not it is explicitly mentioned in its definition.
{{< /tip >}}

The name resolution algorithm is specified to execute thus:

- The search for the object with the name that the unqualified identifier denotes starts in the leftmost schema in the _effective_ path—and if a match is found the algorithm exits.
- If no match is found there, then the search is done anew in the next schema in the _effective_ path, going from left to right—and so on.
- If no match has been found by the time the rightmost schema in the _effective_ path has been searched, then an error is reported.

The test shows that the _pg_catalog_ schema is understood, implicitly, to be to the left of everything that _search_path_ specifies when the _search_path_ doesn't mention this explicitly—in other words, the _effective_ path always includes _pg_catalog_.

{{< note title="The algorithm needs to be more precisely stated to be properly general." >}}
For example, the context of the use of the unqualified identifier might establish the fact that the match must be a function. In this case, the search is satisfied only by finding a function with the matching name _and_ with the correct [subprogram_signature](../syntax_resources/grammar_diagrams/#subprogram-signature).
{{< /note >}}

You might wonder if you could tell name resolution never to try to resolve an unqualified identifier but, rather, simply to fail by not finding the object whose name is denoted. But there's no syntax for this. You might be tempted to try this:

```plpgsql
set search_path = '';
execute qry;
```

This is the new result:

```output
 search_path | row from pg_class 
-------------+-------------------
 ""          | xyz_table
```

The query renders the displayed value of _search_path_ as two adjacent double quotes. This is PostgreSQL's idiomatic way of showing that _search_path_ has been set to the empty string. But executing _qry_ shows that this does not mean that _search_path_ is undefined because name resolution _still_ chooses this table:

```output
pg_catalog.pg_class
```

While there's no syntax to express "never look in _pg_catalog_", you _can_ define _search_path_ by including _pg_catalog_ in the value and by doing so determine the search order. Try this:

```plpgsql
set search_path = "my schema", pg_catalog;
execute qry;
```

This is the new result:

```output
       search_path       |      row from pg_class      
-------------------------+-----------------------------
 "my schema", pg_catalog | xyz in "my schema".pg_class
```

Because _pg_catalog_ is now searched after _my schema_, name resolution now chooses this target:

```output
"my schema".pg_class
```

### Show that name resolution always searches 'pg_temp' as well as 'pg_catalog'

Create a temporary table, populate it with a row to say where it lives, and re-execute the prepared query:

```plpgsql
create temporary table pg_class(relname name, relkind "char");
insert into pg_class(relname, relkind) values('xyz in pg_temp.pg_class', 'r');
execute qry;
```

This is the new result:

```output
       search_path       |    row from pg_class    
-------------------------+-------------------------
 "my schema", pg_catalog | xyz in pg_temp.pg_class
```

This outcome pinpoints a danger. Any role with the privilege to create temporary objects in the current database can create such objects that capture the application's intended objects and thereby subvert its intended behavior. It's good practice to ensure, at least, that _pg_temp_ is always the rightmost schema in the _search_path_:

```plpgsql
set search_path = "my schema", pg_catalog, pg_temp;
execute qry;
```

{{< tip title="Ensure that the role as which client-side sessions connect doesn't have the _temporary_ privilege." >}}
Better still, ensure that the role as which client-side sessions connect doesn't have the _temporary_ privilege on the database that houses the application. (Notice that if the functionality of temporary schema-objects is needed, you can bequeath the ability to create and use them by implementing _security definer_ subprograms for the purpose and by granting _execute_ on these to the client role.)
{{< /tip >}}

This restores the earlier (and presumably intended) result:

```output
           search_path            |      row from pg_class      
----------------------------------+-----------------------------
 "my schema", pg_catalog, pg_temp | xyz in "my schema".pg_class
```

{{< tip title="You can create temporary objects of all kinds but unqualified identifiers work only for creating temporary tables, views, and sequences." >}}
The explicit _create temporary ..._ syntax works only for tables, views, and sequences. But you can create temporary objects of all kinds simply by using a schema qualified identifier whose schema component is _pg_temp_.  See the section [Creating temporary schema-objects of all kinds](../the-sql-language/creating-and-using-temporary-schema-objects/creating-temporary-schema-objects-of-all-kinds/)).
{{< /tip >}}

### Show that user-defined code can subvert the behavior of the native equality operator

The rule that name resolution for a function or an operator never resolves to such a schema object in the _pg_temp_ schema removes the risk of accidental, or deliberate, capture in two cases. But the risk remains, for example when an application developer makes a mistake, that an application schema-object of any kind can capture another one.

The example models the case that the naïve developer understands the importance of including both _pg_catalog_ and _pg_temp_ explicitly in the session's _search_path_ definition but unwisely places user-created schema(s) _ahead_ of _pg_catalog_. Start a session as an ordinary role that has _connect_ and _create_ on the chosen database and set up thus:

```plpgsql
create schema s;

create function s.equals(
  a1 in int,
  a2 in int)
  returns boolean
  -- This function attribute IS wisely specified.
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  select
    case
      when (a1 is null) or (a2 is null) then null
      when (a1 = 17) and (a2 = 42)      then true
      else                                   false
    end;
$body$;

create operator s.= (
  leftarg   = int,
  rightarg  = int,
  procedure = s.equals);

prepare qry as
select
  (1 = 1)::text             as "1 = 1",
  (17 = 42)::text           as "17 = 42",
  (
    17::int
    operator(pg_catalog.=)
    42::int
  )::text                   as "17 = 42 baroque syntax";
```

Now test it using an unwise session-level _search_path_:

```plpgsql
set search_path = s, pg_catalog, pg_temp;
execute qry;
```

This is the result:

```output
 1 = 1 | 17 = 42 | 17 = 42 baroque syntax 
-------+---------+------------------------
 false | true    | false
```

Now test it using a sensible session-level _search_path_:

```plpgsql
set search_path = pg_catalog, s, pg_temp;
execute qry;
```

This is the result:

```output
 1 = 1 | 17 = 42 | 17 = 42 baroque syntax 
-------+---------+------------------------
 true  | false   | false
```

You might argue that this demonstration shows, unremarkably, that you get what you ask for exactly as the rules specify. But it aims to make a point about style and prudence rather than just to illustrate how the rules work. The functions and operators in the _pg_catalog_ schema define, in PostgreSQL and therefore YSQL, what in other RDBMSs is typically hard-coded in the implementation: what most developers would consider to be intrinsic features of SQL like the definitions of the _equality_ and _comparison_ operators, the concatenation operator, and so on, and fundamental functions like _length()_ and _sqrt()_. And the demonstration shows how you can provide your own implementations of these and manipulate the _search_path_ so that your implementations capture the native, documented ones. It shows, also, how you can use baroque syntax to force the use of the native functionality.

It is argued here that it is simply terrible, imprudent style to subvert the native, documented functionality in this way—and to force the use of baroque syntax to regain that functionality.

{{< tip title="If you want to provide alternative implementations for functions and operators that 'pg_catalog' defines, invent your own, unique, names for these." >}}
This is the only sane way to avoid confusion and the risk of buggy maintenance of your application code.
{{< /tip >}}

### No matter what value you set for 'search_path', 'pg_temp' and 'pg_catalog' are always searched

The tests have shown that you cannot exclude either _pg_temp_ or _pg_catalog_ from what name resolution searches. You can, however, control the order.

- If the definition of _search_path_ mentions neither _pg_temp_ nor _pg_catalog_, then the effective search order that name resolution uses is:

  ```output 
  pg_temp, [everything that the definition did mention, in that order], pg_catalog
  ```

- If the definition of _search_path_ does not mention _pg_catalog_ but does mention _pg_temp_, then the effective search order that name resolution uses is:

  ```output
  [everything that the definition did mention, in that order], pg_catalog
  ```

- If the definition of _search_path_ does not mention _pg_temp_ but does mention _pg_catalog_, then the effective search order that name resolution uses is:

  ```output
  pg_temp, [everything that the definition did mention, in that order]
  ```

- If the definition of _search_path_ mentions both _pg_catalog_ and _pg_temp_, then the effective search order that name resolution uses is, of course:

  ```output
  [everything that the definition did mention, in that order]
  ```

{{< tip title="There's no naming convention for 'pg_catalog' objects like functions." >}}
There's no general naming convention to help you choose a name for an application object so that it doesn't collide with that of a _pg_catalog_ object. (Obviously, you shouldn't use names that start with _pg\__ (or _yb\__) for any kind of schema-object.) For example, there are three overloads of a function with the very ordinary name _area()_ in _pg_catalog_. Try this:

```plpgsql
create schema s;
set search_path = pg_catalog, s, pg_temp;
create function s.area(lngth in numeric, hght in numeric)
  returns numeric
  language sql
as $body$
  select lngth*hght;
$body$;
select area(2.0, 3.0);
```

This is the result:

```output
 area 
------
 6.00
```

Here, even if you don't place _s_ leftmost in the _search_path_, name resolution will choose your _s.area()_ because (through the current version) no occurrence of _pg_catalog.area()_ has the right signature. However, you cannot rely on the assumption that a future version will never define a _pg_catalog_ overload that collides with your implementation—and that therefore brings confusion.

Your only recourse is to confirm, when you invent a name for an application object, that it doesn't collide with the name of any object in the _pg_catalog_ schema.
{{< /tip >}}

## Practice recommendation

The forgoing examples have demonstrated the name resolution rules and pointed out the risks that these bring. You clearly must prioritize correct, unsubvertible, application behavior over coding convenience for the application code author and especially over the speed at which you can type _ad hoc_ tests. Remember that nothing can stop any role from setting _search_path_.

- You must ensure that _pg_temp_ is searched _last_.  This is because, without this practice, a role that can create temporary objects can capture an application object with a temporary object with the same name and subvert the application's behavior. If you want to use a temporary object, then you should use a schema-qualified identifier for it that starts with _pg_temp_.
- The decision about where to put _pg_catalog_ in the search order is more subtle. Generally speaking, it's bad practice to create a user-object with a name that collides with that of an object in _pg_catalog_. The objects in _pg_catalog_ define PostgreSQL's implementation, or list facts about such objects—and so a colliding user-defined object would change PostgreSQL's behavior. It's hard to see how doing this would not be confusing and dangerous. This implies putting _pg_catalog_ first in _search_path_. You then need to avoid the possibility of a _pg_catalog_ object capturing an application object by never giving an application object a name that collides with that of a _pg_catalog_ object.

There might be plausible use cases where it isn't known until run-time in which schema the object that's the intended name resolution target will be found, where it might be found in more than one schema, and where the requirement is to resolve to the one that is first on the _search_path_. But such scenarios are rare. Yugabyte therefore recommends that you simply do not rely on name resolution for anything but objects in the _pg_catalog_ schema and adopt this practice;

- Use fully qualified names in the SQL that application code issues unless you can explain, in the design document, why this is unsuitable.
- Set the _search_path_ to just _pg_catalog_, _pg_temp_.

There's no risk that you might create objects in the _pg_catalog_ schema. Connect as the _postgres_ role and try this:

```postgres
set search_path = pg_catalog, pg_temp;
create table t(n int);
```

It causes this error:

```output
42501: permission denied to create "pg_catalog.t"
```

Notice that it _is_ possible, by setting a special configuration parameter, to allow the _postgres_ role to create objects in the _pg_catalog_ schema. But even then, only the _postgres_ role can create objects there. If a bad actor can manage to connect as the _postgres_ role, then all bets are anyway already off.

## Name resolution for secondary objects

Secondary objects, like columns in tables, are resolved within the namespace that the top-level statement defines. PostgreSQL enforces some syntax rules that immediately disambiguate the status of an identifier as a putative schema-object or a putative secondary object where using, say, Oracle Database, the programmer needs to understand tortuous rules. For example, a function invocation in PostgreSQL must always specify the list of actual arguments. This holds even when there are no actual arguments. Here, you must append empty trailing parentheses to the identifier for the function. (Oracle Database allows you, optionally, to elide empty parentheses.)  This means that a select-list item is known to be either a function or a column in a relation _before_ attempting name-resolution. Further, PostgreSQL doesn't support packages and so there cannot be any confusion (even without considering the possible parentheses) between, say, _schema_name.table_name_ and _package_name.function_name_.

For example

```plpgsql
select v, f() from t;
```

can mean only that:

- _t_ is a schema-object of type _table_ or _view_ and that it must be resolved using the reigning value of _search_path_.
- _v_ is a column in the relation _t_ in whatever schema name resolution finds it.
- _f_ is a schema-object of type _function_ and that it must be resolved using the reigning value of _search_path_.

Similarly:

```plpgsql
select s.t.v from t;
```

can only mean that:

- _s_ is a schema.
- _t_ is a relation in the schema _s_.
- _v_ is a column in the relation _s.t_.

And

```postgresql
select (s.t).v from s;
```

can only mean that:

- _s_ is a relation and that it must be resolved using the reigning value of _search_path_.
- _t_ is a column in the relation _s_ in whatever schema name resolution finds it. Moreover, the data type of _t_ is a composite type. (The identity of the composite type can't be discerned by simple inspection of this statement. Rather, this must be determined from the metadata for the relation _s_ in whatever schema name resolution finds it.)
- _v_ is an attribute of the composite type that defines the data type of the column _s.t_.

Notice that the _select_ arguments in these two examples, _s.t.v_ and _(s.t).v_ differ in spelling only in that the second surrounds the first two identifiers with parentheses. But these aren't just decoration to improve readability. Rather, their use conveys essential semantic information about the status of the identified phenomena: _schema_, _table_, and _column_; or _table_, _column_, and _composite type attribute_.

You might think that the first of this pair of examples is very confusingly written and that this would be preferable:

``` plpgsql
select a.v from s.t as a;
```

But the example, as presented, _is_ legal; and in the presence of a matching schema _s_ and a relation _t_ with a column _v_, it executes without error and produces the expected result. The use of the alias _a_ is interesting in its own right. A "keyhole" inspection of _a.v_ tells you that it might mean the column _v_ in the schema-level table _a_ (but not the function _v()_ in the schema _a_). However, analysis of the _from_ list tells the parser that _a_ in the _select_ list can only be the alias _a_ that is defined, privately, for the present statement.

Of course, the engineers who implement Postgres's SQL processing code need to understand all the rules that govern name-resolution for secondary objects in complete and exact detail. But anecdotal evidence tells us that ordinary application programmers who write practical SQL (especially when everything has a sensible name) are able easily to express their meaning without being able to rehearse these rules precisely.

The essential pedagogy of this current section is the explanation of the critical role that the _search_path_ plays in the name resolution of schema-objects.
