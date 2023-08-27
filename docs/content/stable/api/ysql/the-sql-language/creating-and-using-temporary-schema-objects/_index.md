---
title: Creating and using temporary schema-objects [YSQL]
headerTitle: Creating and using temporary schema-objects
linkTitle: Temporary schema-objects
description: Describes how to create temporary schema-objects of all kinds without needing the dedicated create temporary syntax.
menu:
  stable:
    identifier: creating-and-using-temporary-schema-objects
    parent: the-sql-language
    weight: 200
type: indexpage
---

A temporary schema-object can be created at any time during a session's lifetime and lasts for no longer than the session.

{{< note title="Note" >}}
The role that creates a temporary schema-object must have the _temporary_ privilege on the current database.
{{< /note >}}

Apart from their limited lifetime, temporary schema-objects are largely the same, semantically, as their permanent counterparts. But there are critical differences:

- A temporary table's content is private to the session that created it. (By extension, the content of an index on a temporary table is private too.) Moreover, a temporary table uniquely supports the use the special syntax _on commit delete rows_  (see the _[create table](../statements/ddl_create_table)_ section).

- You can see metadata about one session's temporary objects from another session, for as long as the first session lasts. But no session except the one that created a temporary object can use it.

Here are some scenarios where temporary schema-objects are useful.

- Oracle Database supports a schema-object kind called _package_.  A package encapsulates user-defined subprograms together with package-level global variables. Such variables have session duration and the values are private within a single session. But PostgreSQL, and therefore YSQL, have no _package_ construct. A one-column, one-row temporary table can be used to model a scalar package global variable; a one-column, multi-row temporary table can be used to model an array of scalars; and a multi-column, multi-row temporary table can be used to model an array of user-defined type occurrences. 
- Oracle Database supports its equivalent of PostgreSQL's _prepare-and-execute_ paradigm for anonymous PL/SQL blocks as well as for regular DML statements. But PostgreSQL's _prepare_ statement supports only regular DML statements and not the _do_ statement. In Oracle Database, parameterized anonymous PL/SQL blocks are used when the encapsulated steps need to be done several times in a session, binding in different actual arguments each time, during some kind of set up flow, but never need to be done again. A temporary _[language plpgsql](../../user-defined-subprograms-and-anon-blocks/#language-plpgsql-subprograms)_ procedure in PostgreSQL, and therefore in YSQL, meets this use case perfectly.
- See the section [Porting from Oracle PL/SQL](https://www.postgresql.org/docs/11/plpgsql-porting.html) in the PostgreSQL documentation.

Look, now, at each of the following child sections:

- [Temporary tables, views, and sequences](./temporary-tables-views-sequences-and-indexes/)

- [Creating temporary schema-objects of all kinds](./creating-temporary-schema-objects-of-all-kinds/)

- [Demonstrate the globality of metadata, and the privacy of use of temporary objects](./globality-of-metadata-and-privacy-of-use-of-temp-objects/)

- [Recommended on-demand paradigm for creating temporary objects](./on-demand-paradigm-for-creating-temporary-objects/)