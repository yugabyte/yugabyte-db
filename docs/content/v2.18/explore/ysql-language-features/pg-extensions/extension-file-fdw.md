---
title: file_fdw extension
headerTitle: file_fdw extension
linkTitle: file_fdw
description: Using the file_fdw extension in YugabyteDB
menu:
  v2.18:
    identifier: extension-file-fdw
    parent: pg-extensions
    weight: 20
type: docs
---

The [file_fdw](https://www.postgresql.org/docs/11/file-fdw.html) module provides the foreign-data wrapper `file_fdw`, which can be used to access data files in the server's file system, or to execute programs on the server and read their output.

To enable the extension:

```sql
CREATE EXTENSION file_fdw;
```

Create a foreign server:

```sql
CREATE SERVER my_server FOREIGN DATA WRAPPER file_fdw;
```

Now, you can create foreign tables that access data from files. For example:

```sql
CREATE FOREIGN TABLE employees (id int, employee_name varchar) SERVER my_server OPTIONS (filename 'employees.csv', format 'csv');
```

You can execute `SELECT` statements on the foreign tables to access the data in the corresponding files.
