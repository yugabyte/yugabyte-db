---
title: Names and identifiers [YSQL]
headerTitle: Names and identifiers
linkTitle: Names and identifiers
description: Explains the difference between the terms of art 'name' and 'identifier' [YSQL].
menu:
  stable:
    identifier: names-and-identifiers
    parent: api-ysql
    weight: 30
type: docs
---

The terms of art _name_ and _identifier_ are often used interchangeably. But they have different meanings. A simple example makes the point. Authorize a session as a regular role that can create objects in the current database and do this:

```plpgsql
create schema "My Schema";
create table "My Schema"."my_table"(K int primary key, "V" text not null);
insert into "My Schema".MY_table("k", "V") values (1, 17), (2, 42);
select K, "V" from "My Schema".MY_TABLE order by 1;
```

Here's the query result:

```output
 k | V  
---+----
 1 | 17
 2 | 42
```
Query the catalog to list some facts about the just-created table:

```plpgsql
with c("SCHEMA_NAME", "TABLE_NAME", "COLUMN_NAME") as (
  select n.nspname, c.relname, a.attname
  from
    pg_namespace as n
    inner join
    pg_class as c
    on n.oid = c.relnamespace
    inner join
    pg_attribute as a
    on c.oid = a.attrelid
  where n.nspname ~ '^My'::name
  and   c.relname ~ '^my'::name
  and   c.relkind = 'r'
  and   a.attnum > 0)
select * from c order by 1, 2, 3;
```

Notice that the selected columns in the three catalog tables that the query joins are all spelled, in conformance with the PostgreSQL convention for such tables, by starting with an abbreviation that identifies the catalog table and by ending with the string _name_. (In other words, they don't end with the string _identifier_.)

This is the output:

```output
 SCHEMA_NAME | TABLE_NAME | COLUMN_NAME 
-------------+------------+-------------
 My Schema   | my_table   | V
 My Schema   | my_table   | k
```

Notice the presence of both lower case and upper case characters. The _create table_, _insert_, and _select_ statements might have used many different combinations of upper and lower case to produce this same result.

Of course, this example is contrived. You'd never see statements spelled like this in ordinary humanly written real code:

- Double quotes are used, here, around some entirely lower case strings.
- Unquoted strings are variously rendered, here, in all lower case, all upper case, or mixed case.

But the example makes the point forcefully. When we use the terms of art precisely and accurately, we must say that the _create table_,  _insert_ and _select_ statements don't use _names_. Rather they denote the objects with the names of interest by using _identifiers_ to denote the objects with the intended names. The catalog tables list _names_; SQL statements and PL/pgSQL source text are spelled using _identifiers_.

- When a name is upper case or mixed case, the identifier that denotes an object with that name must be exactly faithful to the rendering of case and must be surrounded by double quotes—and the double quote characters are part of the identifier.
- When a name is entirely lower case, the identifier that denotes an object with that name may be written without  double quotes—and if this choice is made, then case has no significance because an unquoted identifier is taken to denote an object with an all lower-case name. The identifier that denotes an object with an all lower-case name may, optionally, be double-quoted—and if it is, then the surrounded name must be written faithfully in all lower case. 

The identifier for the object with the name _my_table_ is written, in the _create table_, _insert_, and _select_ statements above, variously and correctly as _"my_table"_, _MY_TABLE_, and _MY_table_. If a name is entirely lower-case characters and is fairly long, then there's a vast number of different ways in which an identifier that denotes an object with that name might be spelled.

The full treatment of this topic needs to mention punctuation characters too. But the basic point, that _name_ and _identifier_ don't mean the same thing, is adequately made without going to that level of detail. Further, a purist treatment needs to distinguish between the platonic notions, _name_ and _identifier_, and their partner notions, the _text of a name_ and the _text of an identifier_, as these are found in code and in query output. However, this level of precision isn't needed to make the basic point that this section makes.

Finally, it must be acknowledged that the documentation for most SQL systems, including PostgreSQL and YSQL, typically blurs the distinction between _name_ and _identifier_, though both terms of art are used:  _name_ is very frequently used where _identifier_ is the proper choice; and this is most conspicuous in the [Grammar Diagrams](../syntax_resources/grammar_diagrams/). Fortunately, the context always makes the intended meaning clear.