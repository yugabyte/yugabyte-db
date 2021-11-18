---
title: Collations
linkTitle: Collations
description: YSQL collations
headcontent: Collations
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-features-collations
    parent: explore-ysql-language-features
    weight: 410
isTocNested: true
showAsideToc: true
---



## Collation Basics

As in many other databases YSQL also provides collation support. A collation is a set of rules that
define how to compare and sort character strings. The collation support in YSQL is based upon
PostgreSQL with a few restrictions. As in PostgreSQL, most YSQL data types are not related to
character strings and therefore are considered _not collatable_. For example:


```
yugabyte=# create table foo(id int collate "en_US.utf8");
ERROR:  collations are not supported by type integer
LINE 1: create table foo(id int collate "en_US.utf8");
                                ^
```


For character strings data types such as text, varchar and char, we can specify a collation that may
be used to determine how the character string values are compared. Collations are represented by
simple case-insensitive names or by double quoted case sensitive names. The following examples show
how the use of collation can change the comparison results:


```
yugabyte=# select 'a' < 'A' as r;
 r
---
 f
(1 row)

yugabyte=# select 'a' < 'A' collate "en_US.utf8" as r;
 r
---
 t
(1 row)
```


We can see that the use of `collate "en_US.utf8"` changed the ordering between `'a'` and `'A'`.

When no explicit collation is specified, all character string data types have a default collation.

The default collation comes from the database and is initialized to the database collation at
database connection time. Every database has a database character set and a database collation. When
creating a database, PostgreSQL allows the user to specify the collation to use in the new database.
After a database is created, its collation cannot be altered. Currently in YSQL only the "C"
collation can be used as the database collation. As a result, in YSQL the default collation for all
character string data types in "C".


## YSQL Collation Support

Both PostgreSQL and YSQL rely on the operating system for collation support. More specifically,
there are two collation support libraries: libc (the standard C library) and libicu (International
Components for Unicode library). In general libc has more collation variations across different
platforms. For example, on the Linux platform where glibc is used, collation names are internally
normalized by converting a collation name to lowercase and removing special characters. So
`"en_US.UTF-8"` is converted to `"en_US.utf8"`. The Mac OS X libc is part of `libSystem.dylib` and
it does not do the collation name normalization. Therefore libc collation `"en_US.utf8"` is only
valid on Linux platforms while collation `"en_US.UTF-8"` is only valid on Mac platforms. The ICU
library libicu is more platform independent and is generally also considered more robust. For
example, ICU collation `"en-US-x-icu"` is a valid name on both Linux and Mac platforms. Currently
YSQL supports all the OS supplied ICU collations and only a few libc collations such as the "C"
collation and the `"en_US.utf8`? collation. The system catalog table `pg_collation` contains all the
collations that can be used in YSQL, including those predefined collations that are imported from
libc and libicu at initdb time, and any user defined collations created after that.

YSQL imports all the 783 ICU collations provided by the operating system.


```
yugabyte=# select count(collname) from pg_collation where collprovider = 'i';
select count(collname) from pg_collation where collprovider = 'i';
 count 
-------
   783
(1 row)
```



## Collation Creation

In addition to predefined collations, we can define new collations. For example,


```
yugabyte=# create collation nd (provider = 'icu', locale='');
CREATE COLLATION
yugabyte=# select * from pg_collation where collname = 'nd';
-[ RECORD 1 ]-+-------
collname      | nd
collnamespace | 2200
collowner     | 13247
collprovider  | i
collencoding  | -1
collcollate   | 
collctype     | 
collversion   | 153.14
```


We can see that the newly created collation appears in the `pg_collation` table. We can then use the
newly defined collation in table column definition.


```
yugabyte=# create table foo(id text collate "nd", primary key(id));
CREATE TABLE

yugabyte=# insert into foo values (E'El Nin\u0303o');
INSERT 0 1
yugabyte=# insert into foo values (E'El Ni\u00F1o');
INSERT 0 1
```


In libicu, the two strings `E'El Nin\u0303o' `and `E'El Ni\u00F1o'` are equal despite their
different character encodings. Currently YSQL is based upon PostgreSQL 11.2 and only supports
deterministic collations. In a deterministic collation, the two strings `E'El Nin\u0303o'` and `E'El
Ni\u00F1o' `are not equal. Internally when libicu reports two strings are equal, YSQL uses `strcmp`
as a tie-breaker to further compare them. This means that in YSQL two strings are not equal unless
their database character encodings are identical. After YSQL is upgraded to PostgreSQL 13 which
supports non-deterministic collations and does not use any tie-breaker, we also plan to enhance YSQL
to support non-deterministic collations.


## Advantage of Collation

Because in YSQL database collation must be "C", column collation is a nice feature to override the
default "C" collation in order to have a different sort order on the column values. For example, to
enforce a dictionary sort order:


```
yugabyte=# create table foo(name text collate "en_US.utf8");
CREATE TABLE
yugabyte=# insert into foo values ('a'), ('Z');
INSERT 0 2
yugabyte=# select * from foo order by name;
 name
------
 a
 Z
(2 rows)
```


If no column collation is used, we'll see the default sort order that uses the ASCII encoding order.


```
yugabyte=# create table foo(name text);
CREATE TABLE
yugabyte=# insert into foo values ('a'), ('Z');
INSERT 0 2
yugabyte=# select * from foo order by name;
 name
------
 Z
 a
(2 rows)
```



## Index Collation

When a table column has an explicit collation, an index built on the column will be sorted according
to the column collation. YSQL also allows the index to have its own explicit collation that is
different from that of the table column. For example,


```
yugabyte=# create table foo(name text collate "en-US-x-icu");
CREATE TABLE
yugabyte=# create index name_idx on foo(name collate "C" asc);
CREATE INDEX
```


This can be useful to speed up queries that involve pattern matching operators such as LIKE because
a regular index will be sorted according to collation "en-US-x-icu" and such an index cannot be used
by pattern matching operators.


## Collation Strength

YSQL uses the same rules as in PostgreSQL to determine which collation is used in sorting character
strings. An explicitly specified collation has more _strength_ then a referenced column, which has
more strength than a text expression without an explicit collation. For example,


```
yugabyte=# create table foo(name text collate "en-US-x-icu");
CREATE TABLE
yugabyte=# insert into foo values ('a'), ('Z');
INSERT 0 2
yugabyte=# select * from foo where name < 'z';
 name
------
 a
(1 row)

yugabyte=# select * from foo where name < 'z' collate "C";
 name
------
 Z
 a
(2 rows)
```


In the first query, column `'name'` has a collation `"en-US-x-icu"` and the string value `'z' `has
no explicit collation. The comparison is done according to the column collation `"en-US-x-icu"
`because it has more strength. In the second query, string value `'z'` has an explicit collation
`"C" `which is stronger than column collation. Therefore the comparison is done according to the`
"C"` collation ordering rule.


## Collation Usage Context

Collation will not have any effect when used in a non-comparison context and will simply be

discarded. Consider this example:


```
yugabyte=# create table foo(x text collate "C", y text collate "en_US.utf8");
CREATE TABLE
yugabyte=# insert into foo values ('x', 'y');
INSERT 0 1
yugabyte=# select x || y as z from foo;
select x || y as z from foo;
 z
----
 xy
(1 row)
yugabyte=# select x || y as z from foo order by 1;
select x || y as z from foo order by 1;
ERROR:  collation mismatch between implicit collations "C" and "en_US.utf8"
LINE 1: select x || y as z from foo order by 1;
                    ^
HINT:  You can choose the collation by applying the COLLATE clause to one or both expressions.
yugabyte=#
```


The first query does not involve comparison, therefore both column x's collation `"C"` and column
y's collation `"en_US.utf8"` are ignored. The operator `'||' `is not collation sensitive. In the
second query, the result of `x || y` needs to be sorted and thus is used in a comparison context. In
this case YSQL cannot decide the collation to use because both column x and column y have collations
specified and thus have equal strength. It is up to the user to add explicit collation in the query
to resolve the collation mismatch such as


```
yugabyte=# select (x || y) collate "en_US.utf8" as z from foo order by 1;
 z
----
 xy
(1 row)
```



## Expression Collation

Conceptually, every expression of a collatable type has a collation. The collation of a more complex
expression is derived from the collations of its inputs. YSQL and PosgreSQL consider distinct
collation objects to be incompatible even when they have identical properties. For example,


```
yugabyte=# create collation nd from "en_US.utf8";
CREATE COLLATION
yugabyte=# select 'a' collate nd < 'Z' collate "en_US.utf8";
ERROR:  collation mismatch between explicit collations "nd" and "en_US.utf8"
LINE 1: select 'a' collate nd < 'Z' collate "en_US.utf8";
```



## YSQL Collation Restrictions

There are a number of YSQL restrictions on collation due to internal implementation.



* Databases can only be created with "C" collation.

    ```
    yugabyte=# create database db LC_COLLATE = "en_US.utf8" TEMPLATE template0;
    create database db LC_COLLATE = "en_US.utf8" TEMPLATE template0;
    ERROR:  Value other than 'C' for lc_collate option is not yet supported
    LINE 1: create database db LC_COLLATE = "en_US.utf8" TEMPLATE templa...
                               ^
    HINT:  Please report the issue on https://github.com/YugaByte/yugabyte-db/issues
    yugabyte=#
    ```


* Libc collations are very limited

    ```
    yugabyte=# select collname from pg_collation where collprovider = 'c';
      collname  
    ------------
     C
     POSIX
     ucs_basic
     en_US.utf8
     en_US
    (5 rows)
    ```


* Column collation cannot be altered

    ```
    yugabyte=# create table foo(id text);
    CREATE TABLE
    yugabyte=# alter table foo alter column id set data type text collate "en_US.utf8";
    alter table foo alter column id set data type text collate "en_US.utf8";
    ERROR:  This ALTER TABLE command is not yet supported.
    ```


* Pattern matching ops text_pattern_ops, bpchar_pattern_ops, varchar_pattern_ops cannot be used in
* index creation

    ```
    yugabyte=# create table foo(id char(10) collate "en_US.utf8");
    CREATE TABLE
    yugabyte=# create index foo_id_idx on foo(id bpchar_pattern_ops asc);
    ERROR:  could not use operator class "bpchar_pattern_ops" with column collation "en_US.utf8"
    HINT:  Use the COLLATE clause to set "C" collation explicitly.
    ```
