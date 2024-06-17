---
title: Names and identifiers [YSQL]
headerTitle: Names and identifiers
linkTitle: Names and identifiers
description: Explains the difference between the terms of art 'name' and 'identifier' [YSQL].
menu:
  v2.20:
    identifier: names-and-identifiers
    parent: api-ysql
    weight: 30
type: docs
---

The terms of art _name_ and _identifier_ are often used interchangeably. But they have different meanings. A simple example makes the point. See the section [Unquoted and quoted identifiers in action](./#unquoted-and-quoted-identifiers-in-action). Nevertheless, no matter in which order you read this page's two sections ([Artifacts, their names, and identifiers for those names](./#artifacts-their-names-and-identifiers-for-those-names) and [Unquoted and quoted identifiers in action](./#unquoted-and-quoted-identifiers-in-action)), you should make sure that you read both of them.

You should also read the section **[Lexical Structure](https://www.postgresql.org/docs/11/sql-syntax-lexical.html)** in the PostgreSQL documentation.

## Artifacts, their names, and identifiers for those names

{{< note title="The terms of art 'artifact' and 'object'." >}}
The term of art _artifact_ is used to mean anything that has a name and that might be listed in one of the catalog tables or views. Examples are _roles_, _tablespaces_, _databases_, _schemas_, _tables_, _views_, _indexes_, _columns_ in tables, _constraints_, and so on. The term is needed because not all artifacts have an owner. The owning principal can only be a role—but roles don't have owners. Every artifact that isn't a role does have an explicit or implicit owner—and such an owned artifact is denoted by the term of art _object_.

- Roles, databases, and tablespaces exist at the level of the cluster as a whole. But every object of every other kind each exists within a particular database.
- Schemas exist at top-level within a database.
- Many other kinds of object, like tables and views, must each exist at top-level within a particular schema. Such objects are therefore denoted by the term of art _schema object_.
- In contrast, databases, tablespaces, and schemas are denoted by the term of art _non-schema object_.
- Some objects, like _columns_ in tables or _triggers_, can exist only within the context of a schema object. These are denoted by the term of art _secondary object_. It isn't useful to consider the ownership of a secondary object—but you might like to say that a secondary object is owned, transitively, by the role that owns the schema object within whose context it exists.

Top-level SQL statements have items that are defined only within the statement text like _[common_table_expression](../syntax_resources/grammar_diagrams/#common-table-expression)_ and _[alias](../syntax_resources/grammar_diagrams/#alias)_. Such items also have names; and here, too, the name is denoted by an identifier. Items in the text of a _[plpgsql_block_stmt](../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_, like _[formal_arg](../syntax_resources/grammar_diagrams/#formal-arg)_, _[label](../syntax_resources/grammar_diagrams/#label)_, _[variable](../syntax_resources/grammar_diagrams/#variable)_, also have names; and here, too, the name is denoted by an identifier.
{{< /note >}}

The catalog tables list the _names_ of artifacts; SQL statements and PL/pgSQL source text are spelled using _identifiers_ to denote the artifacts to which the statement refers.

- If the name of an artifact satisfies certain rules (for example, the letters that it contains must be lower case _ASCII 7_ letters), then the identifier that denotes it is simply written as the name is written—except that the case with which the identifier is written is insignificant. The term of art _unquoted identifier_  is used for such an identifier; and the term of art _common name_ is used for the name that the unquoted identifier denotes.

- If the name does not satisfy the rules that allow it to be a common name, then it is known by the term of art _exotic name_. The identifier for an exotic name must be written exactly as the name is written and then must be tightly enclosed with double quotes. The double quote characters are, by definition, part of the identifier. The term of art _quoted identifier_ is used for such an identifier.

- The name that is denoted by the identifier for the to-be-created artifact can be arbitrarily long in the _create_ DDL statement for the artifact. (The same holds for other related DDLs like _drop_ and _alter_ for the artifact.)

- But if the length of the name exceeds _sixty-three_ characters, then it will be truncated to _sixty-three_ characters with (according to the current setting for _client_min_messages_) a _42622 notice_ like this:

  ```output
  identifier
  "a123456789a123456789a123456789a123456789a123456789a123456789a123456789"
  will be truncated to
  "a123456789a123456789a123456789a123456789a123456789a123456789a12"
  ```

  (Whitespace was added manually to improve the readability.) Try this demonstration:

  ```plpgsql
  \c yugabyte yugabyte
  set client_min_messages = error;
  
  do $body$
  declare
    ten_ascii7_chars          constant text not null := 'a123456789';
    seventy_ascii7_chars      constant text not null := ten_ascii7_chars||
                                                        ten_ascii7_chars||
                                                        ten_ascii7_chars||
                                                        ten_ascii7_chars||
                                                        ten_ascii7_chars||
                                                        ten_ascii7_chars||
                                                        ten_ascii7_chars;
  
    sixty_three_ascii7_chars  constant text not null := substr(seventy_ascii7_chars, 1, 63);
  
    drop_role constant text not null :=
      'drop role if exists %I';
  
    cr_role constant text not null :=
      'create role %I';
  
    qry constant text not null :=
      $$
        select rolname
        from pg_roles
        where rolname ~'^%s';
      $$;
  
    role_name text not null := '';
  begin
    execute format(drop_role, seventy_ascii7_chars);
    execute format(cr_role,   seventy_ascii7_chars);
  
    execute format(qry, ten_ascii7_chars) into strict role_name;
  
    assert length(sixty_three_ascii7_chars) = 63;
    assert role_name = sixty_three_ascii7_chars;
  
    execute format(drop_role, sixty_three_ascii7_chars);
  end;
  $body$;
  ```

  It finishes silently. To see the truncation notice, change the _set_ statement at the start to this:

  ```plpgsql
  set client_min_messages = notice;
  ```

A common name must satisfy the following criteria in order to have that status.


- _(reprise)_ It must have at most _sixty-three_ characters. (This holds for an exotic name, too.)
- _(reprise)_ Every letter that it contains must be a lower case _ASCII 7_ letter (i.e. in _a_ through _z_).
- It must start with a letter or an _underscore_.
- Each of its remaining characters must be a letter, a digit (i.e. in _0_ though _9_) or the punctuation character _underscore_.

The built-in function _quote_ident()_ will tightly enclose its input with double quotes if the string does not qualify for common name status. Otherwise, it will return the input _as is_. Try this:

```plpgsql
select
  quote_ident('_123') as "Test 1",
  quote_ident('1dog') as "Test 2",
  quote_ident('dog$house') as "Test 3";
```

This is the result:

```output
 Test 1 | Test 2 |   Test 3    
--------+--------+-------------
 _123   | "1dog" | "dog$house"
```

You can confirm this outcome with this test:

```plpgsql
drop schema if exists s cascade;
create schema s;
create table s._123(k int primary key);
```

Notice that if you try _create table s.1dog ..._ instead, it causes the _42601_ syntax error—just as the quoted return from _quote_ident()_ tells you to expect.

Surprisingly, then, this test:

```plpgsql
create table s.dog$house(k int primary key);
```

also completes without error—in spite of the fact that _quote_ident()_ tells you to expect that this would, without quoting the name, cause a syntax error. (Here, too, you can go on to use the table.) The explanation is given in the PostgreSQL documentation in the subsection [Identifiers and Key Words](https://www.postgresql.org/docs/11/sql-syntax-lexical.html#SQL-SYNTAX-IDENTIFIERS) within the _Lexical Structure_ section. Look for this:

> SQL identifiers and key words must begin with a letter (a-z, but also ...) or an underscore. Subsequent characters in an identifier or key word can be letters, underscores, digits (0-9), or dollar signs ($). Note that dollar signs are not allowed in identifiers according to the letter of the SQL standard, so their use might render applications less portable. The SQL standard will not define a key word that contains digits or starts or ends with an underscore, so identifiers of this form are safe against possible conflict with future extensions of the standard.

You can surmise from this that the _quote_ident()_ function aims to implement the stricter rules of the SQL standard. Here is the text that was elided, above, following _"must begin with a letter (a-z, but also"_

> [but also] letters with diacritical marks and non-Latin letters

Try this:

```plpgsql
select
  quote_ident('høyde')  as "Norwegian",
  quote_ident('école')  as "French",
  quote_ident('правда') as "Russian",
  quote_ident('速度')    as "Chinese";
```

This is the result:

```output
 Norwegian | French  | Russian  | Chinese 
-----------+---------+----------+---------
 "høyde"   | "école" | "правда" | "速度"
```

This implies that the identifier that denotes each of those names must be double quoted. But try this test:

```postgresql
create table s.høyde  (k int primary key);
create table s.hØyde  (k int primary key);
create table s.école  (k int primary key);
create table s.École  (k int primary key);
create table s.правда (k int primary key);
create table s.ПРАВДА (k int primary key);
create table s.速度    (k int primary key);
```

Each _create table_ completes without error. This outcome might surprise you. In an unquoted identifier:

- It's only the Latin letters in _a_ through _z_ that are taken _as is_—while the letters in _A_ through _Z_ are taken to mean their lower-case equivalents.
- In contrast, the upper and lower case forms of Latin characters with diacritical marks, like _ø_ and _Ø_, _é_ and _É_ are all taken to be different.
- The upper and lower case forms of all non-Latin characters, in languages like Russian, like `р` and `Р`, `а` and `А`, `д` and `Д` are taken to be different—without considering diacritical marks.
- Pictograms, in languages like Chinese, like `速` and `度` simply have no concept of case.
- It gets even harder to understand the rules if identifiers are written using, say, Hebrew or Arabic script where the reading order is from right to left.

You can use the _quote_ident()_ function to implement a simple _boolean_ function to test if a name is a common name thus: 

```plpgsql
create function s.is_common(nam in text)
  returns boolean
   set search_path = pg_catalog, s
  language sql
as $body$
  select (nam = quote_ident(nam));
$body$;

select
  s.is_common('_123')::text as "is_common('_123')",
  s.is_common('1dog')::text as "is_common('1dog')";
```

This is the result:

```output
 is_common('_123') | is_common('1dog') 
-------------------+-------------------
 true              | false
```

{{< tip title="Use only names that 'quote_ident()' shows don't need quoting for user-created artifacts." >}}
Yugabyte recommends that you use only common names for user-created artifacts and that you determine a candidate's "common" status with _quote_ident()_. In other words, you should not exploit the leniency of PostgreSQL's practical definition that empirical testing, by creating tables with names like _høyde_, _école_, and so on (like the _create table_ attempts above show). The following _do_ statement shows how to test a candidate name's "common" versus "exotic" status:

```plpgsql
do $body$
declare
  nam text not null := '';

  common_names text[] not null := array[
    'employees',
    'pay_grades',
    '_42'];

  exotic_names text[] not null := array[
    'Employees',
    'pay grades',
    'start$dates',
    'emp#',
    '42',
    'høyde',
    'hØyde',
    'école',
    'École',
    'правда',
    'ПРАВДА',
    '速度'];
begin
  foreach nam in array common_names loop
    assert s.is_common(nam), 'Not common';
  end loop;

  foreach nam in array exotic_names loop
    assert not s.is_common(nam), 'Not exotic: '||nam;
  end loop;
end;
$body$;
```

The block finishes without error, demonstrating that the classification of all the tested names is correct. Developers world-wide, with a huge range of native languages and writing schemes, usually avoid the need for quoted identifiers by choosing only names that pass the _is_common()_ test that the block uses.

If you are convinced that you need to go against the recommendation that this tip gives, then you should explain your reasoning in the design specification document for your application's database backend.

Notice that the paradigm for role names, like _d42$api_ that's used in the **[ysql-case-studies](https://github.com/YugabyteDB-Samples/ysql-case-studies)** goes against this tip's recommendations by using _$_ as the separator between the name of the database for which the role is local and the role's so-called nickname. This design choice is justified by reasoning that the names of the two components must each be able to be freely chosen given just that they respect the ordinary criteria for common name status. The _$_ separator therefore has this clear, unique purpose.

The section **[Case study: PL/pgSQL procedures for provisioning roles with privileges on only the current database](../user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/provisioning-roles-for-current-database/)** shows how the name of the database can be isolated to a single point of definition within the code corpus that installs the application's database backend.
{{< /tip >}}

A purist treatment needs to distinguish between the platonic notions, _name_ and _identifier_, and their partner notions, the _text of a name_ and the _text of an identifier_, as these are found in code and in query output. However, this level of precision isn't needed to make the basic point that this section makes; and it's very rare indeed to see any ordinary documentation for a programming language make this distinction.

Finally, it must be acknowledged that the documentation for most SQL systems, including PostgreSQL and YSQL, typically blurs the distinction between _name_ and _identifier_, though both terms of art are used:  _name_ is very frequently used where _identifier_ is the proper choice; and this is most conspicuous in the [Grammar Diagrams](../syntax_resources/grammar_diagrams/) omnibus listing. Fortunately, the context always makes the intended meaning clear.

## Unquoted and quoted identifiers in action

Authorize a session as a regular role that can create objects in the current database and do this:

```plpgsql
drop schema if exists "My Schema" cascade;
create schema "My Schema";
create table "My Schema"."my_table"(K int primary key, "V" text not null);
insert into "My Schema".MY_table("k", "V") values (1, 17), (2, 42);
select K, "V" from "My Schema".MY_TABLE order by 1;
```

The identifier for the object with the common name _my_table_ is written, in the _create table_, _insert_, and _select_ statements above, variously and correctly as _"my_table"_, _MY_TABLE_, and _MY_table_. If a common name is entirely lower-case characters and is fairly long, then there's a vast number of different ways in which an identifier that denotes an object with that name might be spelled.

Here's the query result:

```output
 k | V  
---+----
 1 | 17
 2 | 42
```

Notice that the first column heading spells the column's name, correctly, as lower-case _k_; and that the second column heading spells the column's name, again correctly, as upper-case _V_;  Query the catalog to list some facts about the just-created table:

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

Notice the presence of both lower case and upper case characters. The _create table_, _insert_, and _select_ statements might have used many different combinations of upper and lower case in the identifiers for the common names that produce this same result.

Of course, this example is contrived. You'd never see statements spelled like this in ordinary humanly written real code:

- Double quotes are used, here, around some names that qualify as _common_ and that don't need (but may use) a quoted identifier.
- Unquoted strings are variously rendered, here, in all lower case, all upper case, or mixed case. These therefore identify objects with _common_ names and so the spellings of the names are coerced to lower case.

The example makes the point forcefully. When we use the terms of art precisely and accurately, we must say that the _create table_,  _insert_ and _select_ statements don't use _names_. Rather, they denote the artifacts with the names of interest by using _identifiers_ to denote the artifacts with the intended names.
