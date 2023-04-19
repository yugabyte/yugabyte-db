---
title: Collations
linkTitle: Collations
description: YSQL collations
image: /images/section_icons/secure/create-roles.png
menu:
  preview:
    identifier: advanced-features-collations
    parent: advanced-features
    weight: 240
type: docs
---

## Collation basics

YSQL provides collation support. A _collation_ is a set of rules that define how to compare and sort character strings. YSQL's collation support is based on PostgreSQL with a few [limitations](#ysql-collation-limitations).

As in PostgreSQL, most YSQL data types are not related to character strings and therefore are considered _not collatable_. For example:

```sql
create table coll_tab1(id int collate "en_US");
```

```output
ERROR:  collations are not supported by type integer
LINE 1: create table coll_tab1(id int collate "en_US");
```

For character strings data types such as text, varchar and char, you can specify a collation that may be used to determine how the character string values are compared. Collations are represented by simple case-insensitive names or by double quoted case sensitive names. The following examples show how the use of collation can change the comparison results:

```sql
select 'a' < 'A' as r;
```

```output
 r
---
 f
(1 row)
```

```sql
select 'a' < 'A' collate "en_US" as r;
```

```output
 r
---
 t
(1 row)
```

You can see that the use of `collate "en_US"` changed the ordering between `'a'` and `'A'`.

When no explicit collation is specified, all character string data types have a default collation.

The default collation comes from the database and is initialized to the database collation at database connection time. Every database has a database character set and a database collation. When creating a database, PostgreSQL allows the user to specify the collation to use in the new database. After a database is created, its collation cannot be altered. Currently in YSQL only the "C" collation can be used as the database collation. As a result, in YSQL the default collation for all character string data types in "C".

## YSQL collation support

Both PostgreSQL and YSQL rely on the operating system for collation support. More specifically, there are two collation support libraries: libc (the standard C library) and libicu (International Components for Unicode library).

In general, libc has more collation variations across different platforms. For example, on the Linux platform where glibc is used, collation names are internally normalized by converting a collation name to lowercase and removing special characters. So `"en_US.UTF-8"` is converted to `"en_US"`. The macOS libc is part of `libSystem.dylib` and it does not do the collation name normalization. Therefore libc collation `"en_US"` is only valid on Linux platforms while collation `"en_US.UTF-8"` is only valid on macOS platforms.

The ICU library libicu is more platform-independent, and is generally also considered more robust. For example, ICU collation `"en-US-x-icu"` is a valid name on both Linux and macOS platforms.

Currently, YSQL supports all the OS-supplied ICU collations and only a few libc collations such as the "C" collation and the `"en_US"` collation. The `pg_collation` system catalog table contains all the collations that can be used in YSQL, including the predefined collations that are imported from libc and libicu at initdb time, and any user-defined collations created after that.

```sql
select count(collname) from pg_collation where collprovider = 'i';
```

```output
 count
-------
   783
(1 row)
```

## Collation creation

In addition to predefined collations, you can define new collations. For example,

```sql
create collation nd (provider = 'icu', locale='');
select * from pg_collation where collname = 'nd';
```

```output
 collname | collnamespace | collowner | collprovider | collencoding | collcollate | collctype | collversion
----------+---------------+-----------+--------------+--------------+-------------+-----------+-------------
 nd       |          2200 |     13247 | i            |           -1 |             |           | 153.14
(1 row)
```

You can see that the newly created collation appears in the `pg_collation` table. You can then use the newly defined collation in table column definition.

```sql
create table coll_tab2(id text collate "nd", primary key(id));
insert into coll_tab2 values (E'El Nin\u0303o');
insert into coll_tab2 values (E'El Ni\u00F1o');
```

In libicu, the two strings `E'El Nin\u0303o'` and `E'El Ni\u00F1o'` are equal despite their different character encodings. Currently YSQL is based upon PostgreSQL 11.2 and only supports deterministic collations. In a deterministic collation, the two strings `E'El Nin\u0303o'` and `E'El Ni\u00F1o'` are not equal. Internally when libicu reports two strings are equal, YSQL uses `strcmp` as a tie-breaker to further compare them. This means that in YSQL two strings are not equal unless their database character encodings are identical. After YSQL is upgraded to PostgreSQL 13 which supports non-deterministic collations and does not use any tie-breaker, we also plan to enhance YSQL to support non-deterministic collations.

## Advantage of collation

In YSQL, database collation must be "C", so column collation is a nice feature to override the default "C" collation in order to have a different sort order on the column values. For example, to enforce a dictionary sort order:

```sql
create table coll_tab3(name text collate "en_US");
insert into coll_tab3 values ('a'), ('Z');
select * from coll_tab3 order by name;
```

```output
 name
------
 a
 Z
(2 rows)
```

If no column collation is used, you'll see the default sort order that uses the ASCII encoding order.

```sql
create table coll_tab4(name text);
insert into coll_tab4 values ('a'), ('Z');
select * from coll_tab4 order by name;
```

```output
 name
------
 Z
 a
(2 rows)
```

## Index collation

When a table column has an explicit collation, an index built on the column will be sorted according to the column collation. YSQL also allows the index to have its own explicit collation that is different from that of the table column. For example:

```sql
create table coll_tab5(name text collate "en-US-x-icu");
create index name_idx on coll_tab5(name collate "C" asc);
```

This can speed up queries that involve pattern matching operators such as LIKE because a regular index will be sorted according to collation "en-US-x-icu" and such an index cannot be used by pattern matching operators.

## Collation strength

YSQL uses the same rules as in PostgreSQL to determine which collation is used in sorting character strings. An explicitly specified collation has more _strength_ then a referenced column, which has more strength than a text expression without an explicit collation. For example:

```sql
create table coll_tab6(name text collate "en-US-x-icu");
insert into coll_tab6 values ('a'), ('Z');
select * from coll_tab6 where name < 'z';
```

```output
 name
------
 a
(1 row)
```

```sql
select * from coll_tab6 where name < 'z' collate "C";
```

```output
 name
------
 Z
 a
(2 rows)
```

In the first query, column `'name'` has a collation `"en-US-x-icu"` and the string value `'z'` has no explicit collation. The comparison is done according to the column collation `"en-US-x-icu"` because it has more strength. In the second query, string value `'z'` has an explicit collation `"C"` which is stronger than column collation. Therefore the comparison is done according to the `"C"` collation ordering rule.

## Collation usage context

Collation doesn't have any effect when used in a non-comparison context and is simply discarded. Consider this example:

```sql
create table coll_tab7(x text collate "C", y text collate "en_US");
insert into coll_tab7 values ('x', 'y');
select x || y as z from coll_tab7;
```

```output
 z
----
 xy
(1 row)
```

```sql
select x || y as z from coll_tab7 order by 1;
```

```output
ERROR:  collation mismatch between implicit collations "C" and "en_US"
LINE 1: select x || y as z from coll_tab7 order by 1;
                    ^
HINT:  You can choose the collation by applying the COLLATE clause to one or both expressions.
yugabyte=#
```

The first query doesn't involve comparison, therefore both column x's collation `"C"` and column y's collation `"en_US"` are ignored. The operator `'||'` is not collation sensitive. In the second query, the result of `x || y` needs to be sorted and thus is used in a comparison context. In this case YSQL cannot decide the collation to use because both column x and column y have collations specified and thus have equal strength. It is up to the user to add explicit collation in the query to resolve the collation mismatch such as:

```sql
select (x || y) collate "en_US" as z from coll_tab7 order by 1;
```

```output
 z
----
 xy
(1 row)
```

## Expression collation

Conceptually, every expression of a collatable type has a collation. The collation of a more complex expression is derived from the collations of its inputs. YSQL and PosgreSQL consider distinct collation objects to be incompatible even when they have identical properties. For example:

```sql
create collation nd from "en_US";
select 'a' collate nd < 'Z' collate "en_US";
```

```output
ERROR:  collation mismatch between explicit collations "nd" and "en_US"
LINE 1: select 'a' collate nd < 'Z' collate "en_US";
```

## YSQL collation limitations

There are a number of YSQL limitations on collation due to the internal implementation.

* Databases can only be created with "C" collation:

    ```sql
    create database db LC_COLLATE = "en_US" TEMPLATE template0;
    ```

    ```output
    ERROR:  Value other than 'C' for lc_collate option is not yet supported
    LINE 1: create database db LC_COLLATE = "en_US" TEMPLATE templa...
                               ^
    HINT:  Please report the issue on https://github.com/YugaByte/yugabyte-db/issues
    yugabyte=#
    ```

* libc collations are very limited:

    ```sql
    select collname from pg_collation where collprovider = 'c';
    ```

    ```output
      collname
    ------------
     C
     POSIX
     ucs_basic
     en_US
     en_US
    (5 rows)
    ```

* Column collation can't be altered:

    ```sql
    create table coll_tab8(id text);
    alter table coll_tab8 alter column id set data type text collate "en_US";
    ```

    ```output
    ERROR:  This ALTER TABLE command is not yet supported.
    ```

* Pattern matching operations `text_pattern_ops`, `bpchar_pattern_ops`, and `varchar_pattern_ops` can't be used in index creation:

    ```sql
    create table coll_tab9(id char(10) collate "en_US");
    create index coll_tab9_id_idx on coll_tab9(id bpchar_pattern_ops asc);
    ```

    ```output
    ERROR:  could not use operator class "bpchar_pattern_ops" with column collation "en_US"
    HINT:  Use the COLLATE clause to set "C" collation explicitly.
    ```
