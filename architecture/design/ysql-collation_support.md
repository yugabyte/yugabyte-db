## Support Collation in YSQL


## 1 Collations in Postgres

In postgres, collation specifies how data is sorted and compared. Not every data type supports collation. In fact, most data types in postgres do not support collation. For example,


```
postgres=# create table foo(id int collate "en_US.utf8");
ERROR:  collations are not supported by type integer
LINE 1: create table foo(id int collate "en_US.utf8");
```


To see some collatable types:

 \
`yugabyte=# select typname, typcollation from pg_type where typcollation != 0 order by typname desc limit 3;`


```
  typname  | typcollation 
-----------+--------------
 yes_or_no |          100
 varchar   |          100
 text      |          100
(3 rows)
```


A text column can have collation specified:


```
postgres=# create table foo(id text collate "en_US.utf8");
CREATE TABLE
postgres=# \d foo;
               Table "public.foo"
 Column | Type | Collation  | Nullable | Default 
--------+------+------------+----------+---------
 id     | text | en_US.utf8 |          | 
```


If collation is not specified, postgres uses the "**default**" collation, which is the collation of the currently connected database. The collation of a database is specified in pg_database:


```
postgres=# select datname, datcollate from pg_database where datname = 'postgres';
 datname  | datcollate  
----------+-------------
 postgres | en_US.UTF-8
(1 row)
```


As we can see the database "postgres" has the collation "en_US.UTF-8". In the next example, the id column does not have a collation specified and therefore it will have the database collation "en_US.UTF-8".

  


```
postgres=# create table bar(id text);
CREATE TABLE
postgres=# \d bar;
               Table "public.bar"
 Column | Type | Collation | Nullable | Default 
--------+------+-----------+----------+---------
 id     | text |           |          | 
```


This is identical to setting “default” explicitly:


```
postgres=# create table bar(id text collate "default");
CREATE TABLE
postgres=# \d bar;
               Table "public.bar"
 Column | Type | Collation | Nullable | Default 
--------+------+-----------+----------+---------
 id     | text |           |          | 
```


In order to specify a collation for a table column, a valid collation name must be specified. The pg_collation catalog table contains all the valid collation names:


```
postgres=# select collname from pg_collation;
  collname  
------------
 default
 C
 POSIX
 ucs_basic
 en_US.utf8
 en_US
(6 rows)
```


An error is raised if an invalid collation name is specified:


```
postgres=# create table foo(id text collate "en_CA.utf8");
create table foo(id text collate "en_CA.utf8");
ERROR:  collation "en_CA.utf8" for encoding "UTF8" does not exist
LINE 1: create table foo(id text collate "en_CA.utf8");
```


 \
Even the database collation specified explicitly will not work because the name is not valid according to pg_collation: \
 \
`postgres=# create table foo(id text collate "en_US.UTF-8");`


```
ERROR:  collation "en_US.UTF-8" for encoding "UTF8" does not exist
LINE 1: create table foo(id text collate "en_US.UTF-8")
                                 ^
```


When initdb runs, it inherits from the `LANG` environment variable on my Centos machine as I do not have `LC_ALL` or `LC_COLLATE` set but have `LANG` set to `en_US.UTF-8`. It is valid in the Centos machine, but does not exist in the `pg_collation` table. If `LANG` is not set, locale "C" will be used. I think this is the only collation name that is invalid according to pg_collation but is valid implicitly when collation of text column or data isn’t specified.

Note that column collation does not change the character encoding used by the database to encode the column data. To see the database encoding[^1]:


```
postgres=# show server_encoding;
show server_encoding;
 server_encoding 
-----------------
 UTF8
(1 row)
```


Even if a given collation name is valid, if it cannot work with the current database encoding "UTF8", it will be rejected.


```
postgres=# select oid, collname, collencoding from pg_collation where collname = 'en_US.iso88591';
select oid, collname, collencoding from pg_collation where collname = 'en_US.iso88591';
  oid  |    collname    | collencoding 
-------+----------------+--------------
 12048 | en_US.iso88591 |            8
(1 row)

postgres=# create table foo(id text collate "en_US.iso88591");
create table foo(id text collate "en_US.iso88591");
ERROR:  collation "en_US.iso88591" for encoding "UTF8" does not exist
LINE 1: create table foo(id text collate "en_US.iso88591");
```


In the above example, we can see that "en_US.iso88591" does exist in pg_collation so it is a valid collation name. However it only works with encoding 8 (PG_LATIN1). Because it is not compatible with "UTF8" (PG_UTF8, or integer 6 for collencoding), postgres raises an error.

A single collation name can represent multiple collations. For example


```
postgres=# select oid, collname, collencoding, collcollate, collprovider from pg_collation where collname = 'en_US';
  oid  | collname | collencoding |   collcollate   | collprovider 
-------+----------+--------------+-----------------+--------------
 12047 | en_US    |            8 | en_US           | c
 12638 | en_US    |           16 | en_US.iso885915 | c
 12639 | en_US    |            6 | en_US.utf8      | c
(3 rows)
```


Because one of the collations (with oid 12639) supports "UTF8" encoding, we can use the name "en_US" to specify the collation of the text column, and postgres will automatically choose the right collation "en_US.utf8" to use.


```
postgres=# create table foo(id text collate "en_US");
create table foo(id text collate "en_US");
CREATE TABLE
```


Note that "en_US" needs to be double-quoted because collation names are case sensitive. Identifiers that are not double-quoted are normalized to lower case in postgres:


```
postgres=# create table foo(id text collate en_US);
create table foo(id text collate en_US);
ERROR:  collation "en_us" for encoding "UTF8" does not exist
LINE 1: create table foo(id text collate en_US);
```


Simple collations (C, POSIX, ucs_basic) can simply use memcmp on the actual encoding data to perform sort operation.  For most collations however, postgres needs to call more involved comparison functions that run slower than memcmp.

Postgres supports collation via libraries installed on the host operating system. There are 3 kinds of collation providers:


```
#define COLLPROVIDER_DEFAULT    'd'
#define COLLPROVIDER_ICU        'i'
#define COLLPROVIDER_LIBC       'c'
```


For each collation provider, postgres invokes a corresponding library function to do the proper comparison


<table>
  <tr>
   <td>Provider Name
   </td>
   <td>collprovider in pg_collation
   </td>
   <td>C function
   </td>
   <td>OS library
   </td>
   <td>Collation used
   </td>
  </tr>
  <tr>
   <td>default
   </td>
   <td>‘d’
   </td>
   <td>strcoll
   </td>
   <td>libc.so.6
   </td>
   <td>LC_COLLATE env variable
   </td>
  </tr>
  <tr>
   <td>icu
   </td>
   <td>‘i’
   </td>
   <td>ucol_strcoll
   </td>
   <td>libicui18n.so
   </td>
   <td>column/expression collate
   </td>
  </tr>
  <tr>
   <td>libc
   </td>
   <td>‘c’
   </td>
   <td>strcoll_l
   </td>
   <td>libc.so.6
   </td>
   <td>column/expression collate
   </td>
  </tr>
</table>


Some system catalog tables also have text columns. For example, pg_collation itself has a text column:


```
postgres=# \d pg_collation 
             Table "pg_catalog.pg_collation"
    Column     |  Type   | Collation | Nullable | Default 
---------------+---------+-----------+----------+---------
 collname      | name    |           | not null | 
 collnamespace | oid     |           | not null | 
 collowner     | oid     |           | not null | 
 collprovider  | "char"  |           | not null | 
 collencoding  | integer |           | not null | 
 collcollate   | name    |           | not null | 
 collctype     | name    |           | not null | 
 collversion   | text    |           |          | 
```


When we need to sort on collversion column, because the Collation is not specified, the database collation will be used. However, the database collation will be used via the combination of strcoll() and LC_COLLATE env variable (which is set to the database collation). In order to use strcoll_l or ucol_strcoll, an explicit collation must be specified.

The default provider of a collation should not be confused with the default collation. If a database can be created using an ICU collation whose provider is `icu`, then a table created using the default collation will pick up the database ICU collation. If one can update the pg_collation table to set the collprovider for `default` to `'i'`, then ucol_strcoll will be used for sorting.

The LC_COLLATE environment variable is set by postgres to be the database collation at connection time by calling `pg_perm_setlocale(LC_COLLATE, collate)`. The function `pg_perm_setlocale` calls `putenv` to set `LC_COLLATE` to the collation of the current database being connected. If `putenv` fails, it raises an error saying that the database locale is not compatible with the operating system and then fails the connection.

ICU (International Components for Unicode) library appears to be the direction where postgres is moving to. ICU provides more/better collation support than libc.

To find out which library will be used for a given collation, we can look into pg_collation: \
`postgres=# create table foo(id text collate "en_US.utf8");`


```
CREATE TABLE
postgres=# \d foo;
               Table "public.foo"
 Column | Type | Collation  | Nullable | Default 
--------+------+------------+----------+---------
 id     | text | en_US.utf8 |          | 

postgres=# select collprovider from pg_collation where collname = 'en_US.utf8';
 collprovider 
--------------
 c
(1 row)
```


In the above example, collation “en_US.utf8” is supported via collprovider ‘c’, which has OS library libc.so.6 from the previous table.

For the same text data encoding UTF8, different collations can sort data differently.


```
postgres=# select 'a' collate "C" < 'A';
select 'a' collate "C" < 'A';
 ?column? 
----------
 f
(1 row)

postgres=# select 'a' collate "en_US" < 'A';
select 'a' collate "en_US" < 'A';
 ?column? 
----------
 t
(1 row)
```


We can see that in collation "C" (representing the standard "C" locale), the order of characters (called "collation sequence") in the ASCII character set is the same as the alphabetical order of the characters. So `strcoll_l` produces the same result as `strcmp`. Because the ascii code for 'A' (65) is less than the ascii code of 'a' (97), we see 'A' is sorted before 'a'. Postgres derives the result via strcoll_l, which is equivalent to strcmp for collation "C". In the source code, I saw postgres has a special optimization for collation "C" that uses `memcmp` instead of `strcoll_l` or `strcmp`.

In collation "en_US", the order of characters is different from the order of their UTF8 codes.  Therefore `strcoll_l` returns a different result from `strcmp` for collation "en_US".

We can also see that postgres performs some type inference on collation and deduces that the collation of 'A' to be the same as that of 'a'. In order to compare two text data, they must have compatible collation. This is especially true when both have explicitly specified collations:


```
postgres=# select 'a' collate "en_US" < 'A' collate "C";
select 'a' collate "en_US" < 'A' collate "C";
ERROR:  collation mismatch between explicit collations "en_US" and "C"
```



## 2 Postgres Collation Tie-Breaker

In postgres 11, two collated strings will not be compared as equal unless their DB encodings are identical. For example, \
 \
`postgres=# create collation nd (provider = 'icu', locale='');`


```
CREATE COLLATION
postgres=# create table foo(id text collate "nd", primary key(id));
CREATE TABLE
postgres=# insert into foo values (E'El Nin\u0303o');
INSERT 0 1
postgres=# insert into foo values (E'El Ni\u00F1o');
INSERT 0 1
postgres=# select * from foo;
   id    
---------
 El NinÌo
 El NiÃ±o
(2 rows)
```


According to icol_strcoll, these two strings are considered equal. However, as the above example shows there is no duplicate key error reported. Postgres 11 added a tie-breaker when icol_strcoll returns 0:


```
    /*   
     * In some locales strcoll() can claim that nonidentical strings are
     * equal. Believing that would be bad news for a number of reasons, so we
     * follow Perl's lead and sort "equal" strings according to strcmp().
     */
    if (result == 0)
        result = strcmp(sss->buf1, sss->buf2);
```


This means unless the original values are identical, two strings that are considered by `strcoll` or `icol_strcoll` as equal will not be equal in postgres 11. Since postgres 12, collation is enhanced with the deterministic attribute so that it is more flexible. The following example is from postgres 13.2:


```
postgres=# create collation nd (provider = 'icu', locale='', deterministic = false);
CREATE COLLATION
postgres=# create table foo(id text collate "nd", primary key(id));
CREATE TABLE
postgres=# insert into foo values (E'El Nin\u0303o');
INSERT 0 1
postgres=# insert into foo values (E'El Ni\u00F1o');
ERROR:  duplicate key value violates unique constraint "foo_pkey"
DETAIL:  Key (id)=(El NiÃ±o) already exists.

postgres=# create collation nd (provider = 'icu', locale='', deterministic = true);
CREATE COLLATION
postgres=# create table foo(id text collate "nd", primary key(id));
CREATE TABLE
postgres=# insert into foo values (E'El Nin\u0303o');
INSERT 0 1
postgres=# insert into foo values (E'El Ni\u00F1o');
INSERT 0 1
```


As we can see, under postgres 13.2, we can get different results depending on whether deterministic is true or false. By default, the deterministic parameter is true. If we leave out the `deterministic` parameter, we will not see duplicate key error:


```
postgres=# create collation nd (provider = 'icu', locale='');
CREATE COLLATION
postgres=# create table foo(id text collate "nd", primary key(id));
CREATE TABLE
postgres=# insert into foo values (E'El Nin\u0303o');
INSERT 0 1
postgres=# insert into foo values (E'El Ni\u00F1o');
INSERT 0 1
```


Under postgres 13.2, one can achieve case insensitive equality by creating a fancier collation. For example:


```
postgres=# create collation nd2 (provider = 'icu', locale = '@colStrength=secondary', deterministic = false);
CREATE COLLATION
postgres=# select 'abc' = 'ABC' collate "nd2";
 ?column? 
----------
 t
(1 row)
```


When `deterministic `is set to` false, `postgres will skip the tie-breaker and simply take the result of strcoll, strcoll_l or ucol_strcoll. In the above example, ucol_strcoll returns 0 when comparing ‘abc’ with ‘ABC’ because of `locale = '@colStrength=secondary'.`


## 3 Pattern Matching Operators

Pattern matching operators in postgres include LIKE, SIMILAR TO, and regular expression operators. In postgres implementation, pattern-matching ignores collation sort order perhaps due to practical reason or implementation difficulty. As a result, all pattern matching operators do not support non-deterministic collations. For example,


```
postgres=# create collation nd2 (provider = 'icu', locale = '@colStrength=secondary', deterministic = false);
CREATE COLLATION
postgres=# create table foo(id text collate "nd2");
CREATE TABLE
postgres=# insert into foo values ('abc');
INSERT 0 1
postgres=# select * from foo where id like 'a%';
ERROR:  nondeterministic collations are not supported for LIKE
```


If we change the collation to deterministic, then LIKE operator will work:


```
postgres=# create collation nd2 (provider = 'icu', locale = '@colStrength=secondary', deterministic = true);
CREATE COLLATION
postgres=# create table foo(id text collate "nd2");
CREATE TABLE
postgres=# insert into foo values ('abc');
INSERT 0 1
postgres=# select * from foo where id like 'a%';
 id  
-----
 abc
(1 row)
```


However it is possible that a future postgres release can support pattern matching operators on non-deterministic collations.

Even for a deterministic collation, pattern matching operators will not be able to use an index built on a text column with a deterministic collation. The reason is that the index will still be collation sorted, except when there is a tie in which case postgres uses byte sort comparison. Pattern matching operators can only work on bytes as if the collation were “C” collation. If the index is collation sorted, pattern matching operators cannot use it. In order to let pattern matching operators make use of an index on a collated column, the index must be built with `text_pattern_ops` attribute:


```
postgres=# create index on foo(id text_pattern_ops);
CREATE INDEX
```


Such an index will ignore the column collation and simply assumes as if the column collation were “C”, which ignores the original collation sort rules and just do byte-wise comparisons. In this way, the index can be used by pattern matching operators to improve query performance.


## 4 Abbreviated Key Sorting in Postgres

Postgres has many performance related optimizations and it is no exception for collation sort support. Besides some specific faster code path for the UTF8 encoding (e.g., calling `ucol_strcollUTF8` rather than `ucol_strcoll`) and collation "C", a generic sort support framework on text column is to avoid making calls to `strcoll_l` or `icu_strcoll` when possible because the latter are expensive.  This is done by building an abbreviated key from the original text data in a way such that comparing two abbreviated keys is much faster than calling `strcoll_l` or `icu_strcoll`. When two abbreviated keys are not equal, the result is the same as that of calling `strcoll_l` or `icu_strcoll`. When two abbreviated keys are equal, it does not mean the original strings are equal. In this case, `strcoll_l` or `icu_strcoll` must be invoked to do the full comparison. Putting this sort key optimization in pseudo code using ICU as an example:


```
// a_key is the abbreviated key of a
// b_key is the abbreviated key of b
// Returns an integer like strcmp does
int icu_compare(a, b, a_key, b_key) {
  // First do faster comparison (e.g., as int64 comparison).
  int r = faster_compare(a_key, b_key);
  if (r != 0) {
    return r;
  }
  return icu_strcoll(a, b);
}
```


In case of ICU, computing the abbreviated key of `a` involves two steps:



1. Call `ucol_getSortKey` to get the sort key of `a`
2. Take the first 8 bytes of the sort key of `a` and treat it as an int64 for sort purpose

Apparently, there is a cost to preprocess a set of texts to be compared by computing their abbreviated keys. Once computed however, it is expected that for the common case the text data can be sorted mostly via the "faster_compare" only, therefore short-circuiting `icu_strcoll`. Postgres does have a mechanism to detect cases when two many duplicated abbreviated keys are found. For example, if a text column only has a few possible values, then sorting 1 million of that column values will have too many duplicates. While preprocessing the text data, postgres can find out the abbreviated key optimization does not work well and will abort this optimization and go with `strcoll`, `strcoll_l` or `icu_strcoll` directly.

Given the DocDB storage model, YSQL will not be able to make use of this optimization because all comparisons are done via memcmp on rocksdb keys. This can only become applicable if DocDB is enhanced to support custom comparator function.


## 5 Current Collation Support in YSQL

YSQL currently only supports a smaller set of collations:


```
yugabyte=# select collname, collprovider, collencoding from pg_collation;
 collname  | collprovider | collencoding 
-----------+--------------+--------------
 default   | d            |           -1
 C         | c            |           -1
 POSIX     | c            |           -1
 ucs_basic | c            |            6
(4 rows)
```


The standard POSIX locale is just an alias for the standard C locale. Collation "ucs_basic" behaves like C but sorts by unicode code point. When creating a database in regular postgres, one can specify any valid collation name in the create database statement that is understood by the underlying operating system. For example, in the above 4 collations, centos only understands 2 of them:


```
locale -a | grep -w "default\|C\|POSIX\|ucs_basic"
C
POSIX
```


As a result, we can only create database with collation "C" or "POSIX":


```
postgres=# create database db1 LC_COLLATE = "default" TEMPLATE template0;
create database db1 LC_COLLATE = "default" TEMPLATE template0;
ERROR:  invalid locale name: "default"
postgres=# create database db2 LC_COLLATE = "C" TEMPLATE template0;
create database db2 LC_COLLATE = "C" TEMPLATE template0;
CREATE DATABASE
postgres=# create database db3 LC_COLLATE = "POSIX" TEMPLATE template0;
create database db3 LC_COLLATE = "POSIX" TEMPLATE template0;
CREATE DATABASE
postgres=# create database db4 LC_COLLATE = "ucs_basic" TEMPLATE template0;
create database db4 LC_COLLATE = "ucs_basic" TEMPLATE template0;
ERROR:  invalid locale name: "ucs_basic"
```


In YSQL, the only collation supported is "C", not even "POSIX":


```
yugabyte=# create database db3 LC_COLLATE = "POSIX" TEMPLATE template0;
create database db3 LC_COLLATE = "POSIX" TEMPLATE template0;
ERROR:  Value other than 'C' for lc_collate option is not yet supported
LINE 1: create database db3 LC_COLLATE = "POSIX" TEMPLATE template0;
                            ^
HINT:  Please report the issue on https://github.com/YugaByte/yugabyte-db/issues
```


YSQL does provide collation support in expression context:


```
yugabyte=# select 'a' collate "default" < 'A';
select 'a' collate "default" < 'A';
 ?column? 
----------
 f
(1 row)

yugabyte=# select 'a' collate "ucs_basic" < 'A';
select 'a' collate "ucs_basic" < 'A';
 ?column? 
----------
 f
(1 row)
```


However column collation is not supported:


```
yugabyte=# create table foo (id text collate "ucs_basic");
create table foo (id text collate "ucs_basic");
ERROR:  COLLATE not supported yet
LINE 1: create table foo (id text collate "ucs_basic");
                                  ^
HINT:  See https://github.com/YugaByte/yugabyte-db/issues/1127. Click '+' on the description to raise its priority
```


Because column collation is not supported, there is only one collation in an entire database. That is collation "C":


```
yugabyte=# select datname, datcollate from pg_database;
select datname, datcollate from pg_database;
     datname     | datcollate 
-----------------+------------
 template1       | C
 template0       | C
 postgres        | C
 yugabyte        | C
 system_platform | C
```


The collation "C" is set on every database including template0 and template1.

The relevant initdb code is


```
static void
setlocales(void)
{
    char       *canonname;

    /* Use LC_COLLATE=C with everything else as en_US.UTF-8 as default locale in YB mode. */
    /* This is because as of 06/15/2019 we don't support collation-aware string comparisons, */
    /* but we still want to support storing UTF-8 strings. */
    if (!locale && (IsYugaByteLocalNodeInitdb() || IsYugaByteGlobalClusterInitdb())) {
        const char *kYBDefaultLocaleForSortOrder = "C";
        const char *kYBDefaultLocaleForEncoding = "en_US.UTF-8";

        locale = pg_strdup(kYBDefaultLocaleForEncoding);
        lc_collate = pg_strdup(kYBDefaultLocaleForSortOrder);
        fprintf(
            stderr,
            _("In YugabyteDB, setting LC_COLLATE to %s and all other locale settings to %s "
              "by default. Locale support will be enhanced as part of addressing "
              "https://github.com/yugabyte/yugabyte-db/issues/1557\n"),
            lc_collate, locale);
    }
```



## 6 Support collation in YSQL

It would not be hard to support collation if we disallow indexing on any collation column. All we need to do is to write the column text data to docdb as we currently do. When the column data is read back into postgres, they can be sorted properly via the abbreviated key sort support framework.

The challenge comes when we have to support indexing on a collation column, or specify primary key with such a column. In such a case, we cannot simply write the text data out as docdb keys because their collation order can be different from their byte-sorted order. In other words, given any arbitrary text a and b, we may have:


```
  icu_strcoll(a, b) != strcmp(a, b);
```


  

In order to ensure docdb index sort properly for a given collation, we must transform the text data as keys that are _collation sorted_ and write these keys as docdb keys. In other words, we need to encode the text data in a way such that:


```
  icu_strcoll(a, b) == strcmp(a_docdb_key, b_docdb_key)
```


where a_docdb_key is a docdb encoding of text a, and b_docdb_key is a docdb encoding of text b.

  


## 7 Compute docdb_key for collated text

Fortunately, both libc and libicui18n provide their ways to compute a collation sorted key for a given string. In libc, there is `strxfrm`. The `strxfrm` function transforms the string in such a way that if we call `strcmp` with the two transformed strings, the result is the same as a call to `strcoll` applied to the original two strings. In libicui18n, there is `ucol_getSortKey` that does the same transformation. Both functions are used in postgres source code:


```
        bsize = strxfrm(sss->buf2, sss->buf1, sss->buflen2);

        bsize = ucol_getSortKey(sss->locale->info.icu.ucol,
                                uchar, ulen,
                                (uint8_t *) sss->buf2, sss->buflen2);
```


In YSQL, we compute docdb_key for every data type. For example, a positive integer is not written literally as is. Instead, its highest bit is set to 1 to get the encoded docdb_key. The docdb_key has the property of [order embedding](https://en.wikipedia.org/wiki/Order_embedding) and is written as part of a key in the docdb. Both `strxfrm` and `ucol_getSortKey `provide order embedding transformations of the original text data.


## 8 Storing collation text data in DocDB

Although `strxfrm` or `ucol_getSortKey` can easily return a collation sorted key, the encoding is not reversible. There is no way to reverse that encoding to recover the original text data.  In other words, the algorithm of `strxfrm` or `ucol_getSortKey` does a lossy conversion. They both need to normalize the input string because in unicode, two different strings can be considered to be equivalent and the unicode standard does not promote one over the other. In order to compute a unique sort key, the input string is first normalized[^2] (aka canonicalized). That is a lossy step and that's why the sort key encoding is not reversible.

This is very different from say an integer type column where we only need to store the encoded integers as docdb keys. The original integer value can be easily recovered from its docdb key encoding and we can have an index-only scan that needs to return the original integer back to postgres.

  

If a collated text column is also a primary key, then we clearly need to store two string:

(1) The collation sorted key

(2) The original text

(1) is needed for storing in docdb which requires that keys are sorted. (2) is needed for retaining the original text data because it cannot be derived from (1).

For an index collated text column, if we don't support index-only scan, then we just need to store the sorted key. If we want to support index-only scan, then we also need to store the original text in the same way as in the primary key case. Otherwise index scan can only return the sorted key back to postgres which will then misinterpret the sorted key as the original text data.

For a regular column, we only need to store the original text.


### 8.1 How text data is stored in DocDB?


```
yugabyte=# create table bar(id1 text, id2 text, primary key (id1));
create table bar(id1 text, id2 text, primary key (id1));
CREATE TABLE
yugabyte=# insert into bar values ('a', 'a');
insert into bar values ('a', 'a');
INSERT 0 1
yugabyte=# select id1, id2 from bar;
select id1, id2 from bar;
 id1 | id2 
-----+-----
 a   | a
```


The above example creates a single row in YSQL table `bar`, it is represented as a document that has 2  key-value (k, v) pairs stored in the docdb:


```
(47AD655361000021214A8023800191F115E1AE73804A, 24)
(47AD655361000021214B8B23800191F115E1AE73803FAB, 5361)
```


Let’s apply  [online doc](https://docs.yugabyte.com/latest/architecture/docdb/persistence/) to this example, each key has 3 parts:



*   a doc key: composed of a 16-bit hash, 1 hashed column, 0 range column
*   a subkey: has just one value column
*   DocHybridTime: composed of physical time, logical time, write id

The details of the first key-value pair is listed below:


<table>
  <tr>
   <td>HexCode
   </td>
   <td>Comment
   </td>
  </tr>
  <tr>
   <td>47
   </td>
   <td>ValueType::kUInt16Hash
   </td>
  </tr>
  <tr>
   <td>AD65
   </td>
   <td>16-bit hash value (44389)
   </td>
  </tr>
  <tr>
   <td>53
   </td>
   <td>ValueType::kString
   </td>
  </tr>
  <tr>
   <td>610000
   </td>
   <td>the string literal 'a', ended with two zeros to account for embedded zero byte
   </td>
  </tr>
  <tr>
   <td>21
   </td>
   <td>ValueType::kGroupEnd (hashed group end)
   </td>
  </tr>
  <tr>
   <td>21
   </td>
   <td>ValueType::kGroupEnd (range group end)
   </td>
  </tr>
  <tr>
   <td>4A
   </td>
   <td>ValueType::kSystemColumnId
   </td>
  </tr>
  <tr>
   <td>80
   </td>
   <td>The value of kLivenessColumn, 0 is encoded as 80 with highest bit turned on
   </td>
  </tr>
  <tr>
   <td>23
   </td>
   <td>ValueType::kHybridTime
   </td>
  </tr>
  <tr>
   <td>80
   </td>
   <td>The value 0, Hybrid time generation number, currently always 0
   </td>
  </tr>
  <tr>
   <td>0191F115E1AE73
   </td>
   <td>The embedded physical value in microseconds, 7 bytes long
   </td>
  </tr>
  <tr>
   <td>80
   </td>
   <td>The embedded 12-bit Logical value (0 in this example)
   </td>
  </tr>
  <tr>
   <td>4A
   </td>
   <td>A combo of write_id + 1 (0 + 1 in this example) &lt;< 5 |  entire DocHybridTime encoded size (10 in the lowest 5-bit).
   </td>
  </tr>
  <tr>
   <td>24
   </td>
   <td>ValueType::kNullLow
   </td>
  </tr>
</table>


The second key-value pair is similar to the first one, with these differences:


<table>
  <tr>
   <td>HexCode
   </td>
   <td>Comment
   </td>
  </tr>
  <tr>
   <td>4B
   </td>
   <td>ValueType::kColumnId
   </td>
  </tr>
  <tr>
   <td>8B
   </td>
   <td>value of column id2 is 11, encoded as 8B with highest bit turned on
   </td>
  </tr>
  <tr>
   <td>3FAB
   </td>
   <td>A combo of write_id + 1 (1 + 1 in this example) &lt;< 5 | entire DocHybridTime encoded size (lowest 5-bit). Note that write id 1 is encoded as 2 bytes, but write id 0  is encoded as just 1 byte. Therefore the first DocHybridTime encoded as 10 bytes but the second DocHybridTime 11 bytes.
   </td>
  </tr>
  <tr>
   <td>53
   </td>
   <td>ValueType::kString
   </td>
  </tr>
  <tr>
   <td>61
   </td>
   <td>string literal 'a' without ending zeros
   </td>
  </tr>
</table>



### 8.2 How to store collated text data in DocDB?

Note that in the first key-value pair, the value is just `ValueType::kNullLow`. This is because the id1 column value 'a' is already encoded in the doc key itself and can be truthfully recovered from the doc key. This is no longer true for collated text where we will have to store the sort key of 'a' instead of 'a' itself. But as described above we cannot derive the original value 'a' from its sort key so if we only store `ValueType::kNullLow` as the value then we'll lose the original value of 'a'. Depending on whether the collation is deterministic or not (since postgres 12), we need to store the doc key differently.

To support non-deterministic collations, where postgres will skip its “tie-breaker” code and simply use the return result of `strcoll`, `strcoll_l` or `ucol_strcoll`, then we cannot simply append the original text ‘a’ to the end of the sort key and make it part of the doc key because ‘a’ is not byte-sortable which will result in the entire doc key not byte-sortable. Therefore, in addition to storing the sort key as part of the doc key, we also need to store the original value 'a' as the value, just as we do in the second key-value pair where id2 is not part of the key. So the first key-value pair would change from


```
(47AD655361000021214A8023800191F115E1AE73804A, 24)
```


to


```
(47AD655361000021214A8023800191F115E1AE73804A, 5361)
```


To support the deterministic collations (postgres 11 only allows deterministic collations and for postgres 12 or above they are still the default), we can simply append the original text ‘a’ to the end of the sort key to mimic the postgres “tie-break” behavior. Even in a multi-column key scenario, this should work well and will break the tie in exactly the same way as postgres when two sort keys are equal but two values are different.

Q: Should we use a new value type `ValueType::kCollatedString` or `ValueType::kSortKey` instead of `ValueType::kString `in the doc key`?`


### 8.3 How to store non-collated text data in DocDB?

We should continue to only store the text data when the column collation is one of "C", "POSIX", or "ucs_basic". Collation "C" and "POSIX" are identical and they work with all encodings. They simply do memcmp on byte sequences regardless of the database encoding. This is indicated by their `collencoding` column set to -1, which means "all" encodings.

  


```
yugabyte=# select collname, collencoding, collcollate from pg_collation;
 collname  | collencoding | collcollate 
-----------+--------------+-------------
 default   |           -1 | 
 C         |           -1 | C
 POSIX     |           -1 | POSIX
 ucs_basic |            6 | C
(4 rows)
```


Although ucs_basic only works with "UTF8" encoding (PG_UTF8 or integer 6), its collcollate attribute is also "C". Therefore it also uses memcmp based sort comparison. For any collation that we can use memcmp for sorting, there is no need to store a docdb_key and the original text data. In other words, we'll have a_docdb_key == a and b_docdb_key == b, so store only a and b is more space efficient.  

So far in YSQL, we always store the same encoded data regardless of index key or column value. It appears that in order to support indexing on collated text columns and continue to maintain storage space efficiency, we need to distinguish between index and column value: indexes including primary key requires double storage of sorted key and original text, column value only requires storing the original text.


## 9 Expression Push Down

YSQL has an expression push down optimization to let the docdb in the tablet server evaluate an expression on a collection of rows. The advantage is to bring compute and data together because data is stored in docdb on the tablet server. We only need to send the expression across the network and bring back the final result set.  In contrast, if we do expression evaluation in postgres, then we need to first fetch all the row data from the tablet server which might need a network trip, then evaluate the expression within the postgres process. Often this involves more network traffic and the result set.

However, if an expression involves an operation that requires access to the collation metadata, then currently the tablet server does not have access to `pg_collation` and will not be able to evaluate such an expression. In this case we should disallow the expression push down optimization. All the row data must be brought into postgres to do the expression evaluation.

For index scan, docdb only does comparison operations on collated text data. This can be accomplished via `memcmp(a_docdb_key, b_docdb_key)` and does not need collation metadata.

  

In short, docdb cannot perform any operation that requires access to collation metadata, postgres must prevent any such expression leak into docdb. In addition, docdb cannot generate any internal operation that requires access to collation metadata. I think we can assume that will not happen.

  

Finally, if we do not store the sort key in a collated text column, then docdb cannot do any comparison that involves column text data. Docdb only uses `memcmp` for comparison, and that can only work on sorted keys, not the original text data. Because docdb is a key value store, it should only compare keys, not values. In case of collated text, values are not byte-comparable! Hopefully we can assume that this will not happen either.

Whenever we build a PgConstant for a collated text data, we must ensure that the collation of the text must match exactly the column collation. If postgres does not already ensure this, we must adapt postgres specifically for Yugabyte context to prevent any collation mismatch from happening. In the following postgres examples, it appears that postgres is already doing a good job to prevent collation mismatch when index only scan is used:


```
 postgres=# \d foo;
               Table "public.foo"
 Column | Type | Collation  | Nullable | Default 
--------+------+------------+----------+---------
 id     | text | en_US.utf8 | not null | 
Indexes:
    "foo_pkey" PRIMARY KEY, btree (id)

postgres=# explain select id from foo order by id;
                                  QUERY PLAN                                  
------------------------------------------------------------------------------
 Index Only Scan using foo_pkey on foo  (cost=0.15..68.55 rows=1360 width=32)
(1 row)

postgres=# explain select id from foo order by id collate "en_US.utf8";
                                  QUERY PLAN                                  
------------------------------------------------------------------------------
 Index Only Scan using foo_pkey on foo  (cost=0.15..68.55 rows=1360 width=64)
(1 row)

postgres=# explain select id from foo order by id collate "C";
                          QUERY PLAN                          
--------------------------------------------------------------
 Sort  (cost=94.38..97.78 rows=1360 width=64)
   Sort Key: id COLLATE "C"
   ->  Seq Scan on foo  (cost=0.00..23.60 rows=1360 width=64)
(3 rows)
postgres=# explain select id from foo order by id collate "en_CA.utf8";
                          QUERY PLAN                          
--------------------------------------------------------------
 Sort  (cost=94.38..97.78 rows=1360 width=64)
   Sort Key: id COLLATE "en_CA.utf8"
   ->  Seq Scan on foo  (cost=0.00..23.60 rows=1360 width=64)
(3 rows)
```


Note when there is a mismatch between the column collation “en_US.utf8” and the explicitly specified collation “C” or even “en_CA.utf8” (English Canada), postgres does not do index-only scan. It only performs index-only scan when there is no mismatch. This is important for YSQL because when comparing docdb keys against a sorted key of a collated text, the sorted key must be generated on the same collation of the column. Otherwise the comparison will be invalid.


```
postgres=# explain select * from foo where id = 'c';
                                QUERY PLAN                                
--------------------------------------------------------------------------
 Index Only Scan using foo_pkey on foo  (cost=0.15..8.17 rows=1 width=32)
   Index Cond: (id = 'c'::text)
(2 rows)

postgres=# explain select * from foo where id = 'c' collate "en_US.utf8";
                                QUERY PLAN                                
--------------------------------------------------------------------------
 Index Only Scan using foo_pkey on foo  (cost=0.15..8.17 rows=1 width=32)
   Index Cond: (id = 'c'::text COLLATE "en_US.utf8")
(2 rows)

postgres=# explain select * from foo where id = 'c' collate "C";
                     QUERY PLAN                      
-----------------------------------------------------
 Seq Scan on foo  (cost=0.00..27.00 rows=1 width=32)
   Filter: (id = 'c'::text COLLATE "C")
(2 rows)

postgres=# explain select * from foo where id = 'c' collate "en_CA.utf8";
                     QUERY PLAN                      
-----------------------------------------------------
 Seq Scan on foo  (cost=0.00..27.00 rows=1 width=32)
   Filter: (id = 'c'::text COLLATE "en_CA.utf8")
(2 rows)
```


When there is a collation mismatch, postgres generates a sequential scan on the base table. That will bring the column text data (not the sort key) back into the postgres process. Inside the postgres process  the column text (in database encoding) will be sorted or compared properly according to that explicitly specified collation, not the original column collation.


```
postgres=# insert into foo values ('c' collate "en_US.iso88591");
ERROR:  collation "en_US.iso88591" for encoding "UTF8" does not exist
LINE 1: insert into foo values ('c' collate "en_US.iso88591");
                                    ^
```


Because the collation “en_US.iso88591” does not work with UTF8, postgres raises an error.


```
postgres=# insert into foo values ('c' collate "en_CA.utf8");
INSERT 0 1
```


In the above example, there is a _seemingly mismatch_ between the column collation “en_US.utf8” and the value collation “en_CA.utf8”. However, recall that collation does not change the actual database encoding. In this context, the collation of the value ‘c’ is only checked against the database encoding (UTF8), not against the column collation “en_US.utf8”. As long as it works with UTF8, it is semantically dropped from then on. Therefore what’s sent down to docdb is the value ‘c’ in collation “en_US.utf8”, not “en_CA.utf8”.

We must do a thorough inspection of the  postgres source code to ensure that only when collations match exactly can YSQL make a PgConstant and send to docdb.


## 10 How to Separate Key and Non-key Contexts

In theory in postgres one can always figure out the context of a text variable or constant. If it is a key context, we send key + text to docdb. If it is a non-key context, we only need to send the text. However in practice it appears to be rather tricky and fragile. For example, by the time we are making a PgConstant to wrap a collated text value, it is deep down in the call stack and hard to access the postgres context metadata. We must make significant code changes to pass that key vs non-key context all the way down, and we must remember to do that for all the applicable places, which can involve all expression contexts.

  

Rather than making such a delicate decision in postgres, it seems both easier and safer for docdb to make that decision. When postgres is ready to push a PgConstant to docdb, we can always compute the sort key if the text has specified a collation. The sort key is prepended before the original text data and we do not care whether it is a key or non-key context. We'll just waste some effort for a non-key context. The combined string is then sent to docdb. If we can add a flag in this combined string indicating that (e.g., a new docdb datatype), then docdb can decide whether to strip off the sort key. If the string is to be written as a key, then the entire string will be stored. Otherwise, the sort key is stripped off and only the original text data will be stored. In all other expression contexts, the sort key will be kept and we can expect that it will only be used for comparison purposes.

Stripping off the sort key can also be done inside pggate code if there is a single point where we have enough context information to deduce that this is a non-key column write, as we do want to keep the sort key in all other contexts and send it to docdb. If that can be done, then we do not need to touch docdb and the code can be even simpler as this logic does not need to span across pggate and docdb.


## 11 Collation Stability Issue

It is known that different ICU library versions may change the collation sort order and therefore sorted keys may not be stable across ICU versions. For postgres, only the original text data is stored, once upgraded to pick up a newer ICU library that is not compatible with the previous version, it will warn the user and offer some guidelines and tools to fix things. This will be even more of an issue for YSQL because we have already stored sorted keys in the tablets. A newer ICU will only allow new data to be stored according to the new sort order. The existing sort keys may no longer be 100% valid. This is an implementation restriction of docdb. Users will have to find their workarounds such as rebuild indexes and rewrite collatable primary key tables in order to get in sync with the new ICU library.


## 12 The Proposal for YSQL Collation Support

Because YSQL is still on postgres 11.2, we can only support deterministic collations at present. Deterministic collations still do collated comparisons via strcoll, strcoll_l, or ucol_strcoll. Only when there is a tie,  strcmp is used as a tie-breaker. This property makes the YSQL support easier to implement precisely:



*   For a collated text key column, we store the sort key followed by the original text value.
*   For a collated text non-key column, we store the original text value as is.

The cost is that we have to store the value twice: one in encoded form (the sort key), one in original form. However, that appears to be the price to pay within the current DocDB infrastructure which does not have custom comparator support. In general, an index is used for performance reasons, and there is a space cost associated with it. An index on a collated text column simply incurs more space cost.

I propose that we defer the support of non-deterministic collations until after YSQL is refreshed to postgres 12 or 13. At that time we can come back to the design to reconsider the use of a custom comparator which requires enhanced DocDB infrastructure support. Even with a custom comparator support, for existing 11.2 tables that do not have a custom comparator,  we can still continue to use the double-storage approach to maintain backward compatibility. Customers can rebuild the index to pick up the new format that uses a custom comparator to save index storage space.


## 13 References

[Posgres Collation Support](https://www.postgresql.org/docs/11/collation.html)

[ICU Documentation](https://unicode-org.github.io/icu/userguide/collation/api.html#getsortkey)

[Libc Locales and Internationalization](https://www.gnu.org/software/libc/manual/html_node/Locales.html)

[Yugabyte Storage Model](https://docs.yugabyte.com/latest/architecture/docdb/persistence/)

[Implementing Unicode Collation in CockroachDB](https://www.cockroachlabs.com/blog/unicode-collation-in-cockroachdb/)

[Nondeterministic collations](https://postgresql.verite.pro/blog/2019/10/14/nondeterministic-collations.html)


<!-- Footnotes themselves at the bottom. -->
## Notes

[^1]:
     Postgres allows different client encoding and server encoding which pg_conversion allows. The server does the conversion both ways via function `perform_default_encoding_conversion `by specifying the `is_client_to_server` argument.

[^2]:
     YSQL also does normalization for floating point types, see `util::CanonicalizeFloat` and `util::CanonicalizeDouble`. For floating point numbers, we only store the canonicalized value and that is considered a well-accepted industry practice.
