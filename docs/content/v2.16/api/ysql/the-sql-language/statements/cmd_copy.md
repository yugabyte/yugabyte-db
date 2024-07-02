---
title: COPY statement [YSQL]
headerTitle: COPY
linkTitle: COPY
description: Transfer data between tables and files with the COPY, COPY TO, and COPY FROM statements.
menu:
  v2.16:
    identifier: cmd_copy
    parent: statements
type: docs
---

## Synopsis

Use the `COPY` statement to transfer data between tables and files. `COPY TO` copies from tables to files. `COPY FROM` copies from files to tables. `COPY` outputs the number of rows that were copied.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-bs-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-bs-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/copy_from,copy_to,copy_option.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/copy_from,copy_to,copy_option.diagram.md" %}}
  </div>
</div>

## Semantics

### *table_name*

Specify the table, optionally schema-qualified, to be copied.

### *column_name*

Specify the list of columns to be copied. If not specified, then all columns of the table will be copied.

### *query*

Specify a `SELECT`, `VALUES`, `INSERT`, `UPDATE`, or `DELETE` statement whose results are to be copied. For `INSERT`, `UPDATE`, and `DELETE` statements, a RETURNING clause must be provided.

### *filename*

Specify the path of the file to be copied. An input file name can be an absolute or relative path, but an output file name must be an absolute path. Critically, the file must be located _server-side_ on the local filesystem of the YB-TServer that you connect to.

To work with files that reside on the client, nominate `stdin` as the argument for `FROM` or `stdout` as the argument for `TO`.

Alternatively, you can use the `\copy` metacommand in [`ysqlsh`](../../../../../admin/ysqlsh#copy-table-column-list-query-from-to-filename-program-command-stdin-stdout-pstdin-pstdout-with-option).

### *stdin* and *stdout*

Critically, these input and output channels are defined _client-side_ in the environment of the client where you run  [`ysqlsh`](../../../../../admin/ysqlsh#copy-table-column-list-query-from-to-filename-program-command-stdin-stdout-pstdin-pstdout-with-option) or your preferred programming language. These options request that the data transmission goes via the connection between the client and the server.

If you execute the `COPY TO` or `COPY FROM` statements  from a client program written in a language like Python, then you cannot use ysqlsh features. Rather, you must rely on your chosen language's features to connect `stdin` and `stdout` to the file that you nominate.

However, if  you execute `COPY FROM` using  [`ysqlsh`](../../../../../admin/ysqlsh#copy-table-column-list-query-from-to-filename-program-command-stdin-stdout-pstdin-pstdout-with-option), you have the further option of including the `COPY` invocation at the start of the file that you start as a `.sql` script. Create a test table thus:

```plpgsql
drop table if exists t cascade;
create table t(c1 text primary key, c2 text, c3 text);
```

And prepare `t.sql` thus:

```
copy t(c1, c2, c3) from stdin with (format 'csv', header true);
c1,c2,c3
dog,cat,frog
\.
```

Notice the `\.` terminator. You can simply execute `\i t.sql` at the  [`ysqlsh`](../../../../../admin/ysqlsh#copy-table-column-list-query-from-to-filename-program-command-stdin-stdout-pstdin-pstdout-with-option) prompt to copy in the data.

{{< note title="Some client-side languages have a dedicated exposure of COPY" >}}

For example, the _"psycopg2"_ PostgreSQL driver for Python (and of course this works for YugabyteDB) has dedicated cursor methods for `COPY`. See [Using COPY TO and COPY FROM](https://www.psycopg.org/docs/usage.html#using-copy-to-and-copy-from).

{{< /note >}}

## Copy options

### ROWS_PER_TRANSACTION

The ROWS_PER_TRANSACTION option defines the transaction size to be used by the `COPY` command.

Default: 20000 for YugabyteDB versions 2.14 and 2.15 or later, and 1000 for prior versions.

For example, if the total tuples to be copied are 5000 and `ROWS_PER_TRANSACTION` is set to 1000, then the database will create 5 transactions and each transaction will insert 1000 rows. If there is an error during the execution of the copy command, then some tuples can be persisted based on the already completed transaction. This implies that if an error occurs after inserting the 3500th row, then the first 3000 rows will be persisted in the database.

- 1 to 1000 →  Transaction_1
- 1001 to 2000 → Transaction_2
- 2001 to 3000 → Transaction_3
- 3001 to 3500 → Error

First 3000 rows will be persisted to the table and `tuples_processed` will show 3000.

### REPLACE

The `REPLACE` option replaces the existing row in the table if the new row's primary/unique key conflicts with that of the existing row.

Note that `REPLACE` doesn't work on tables that have more than 1 unique constraints (see [#13687](https://github.com/yugabyte/yugabyte-db/issues/13687) for explanation)

Default: by default conflict error is reported.

### DISABLE_FK_CHECK

The `DISABLE_FK_CHECK` option skips the foreign key check when copying new rows to the table.

Default: by default, foreign key check is always performed when `DISABLE_FK_CHECK` option is not provided.

### SKIP n

The `SKIP n` option skips the first `n` rows of the file. `n` must be a non-negative integer ().

Default: 0, no rows are skipped.

## Examples

The examples below assume a table like this:

```plpgsql
yugabyte=# CREATE TABLE users(id BIGSERIAL PRIMARY KEY, name TEXT);
yugabyte=# INSERT INTO users(name) VALUES ('John Doe'), ('Jane Doe'), ('Dorian Gray');
yugabyte=# SELECT * FROM users;
 id |    name
----+-------------
  3 | Dorian Gray
  2 | Jane Doe
  1 | John Doe
(3 rows)
```

### Export an entire table

Copy the entire table to a CSV file using an absolute path, with column names in the header.

```plpgsql
yugabyte=# COPY users TO '/home/yuga/Desktop/users.txt.sql' DELIMITER ',' CSV HEADER;
```

### Export a partial table using the WHERE clause with column selection

In the following example, a `WHERE` clause is used to filter the rows and only the `name` column.

```plpgsql
yugabyte=# COPY (SELECT name FROM users where name='Dorian Gray') TO '/home/yuga/Desktop/users.txt.sql' DELIMITER
 ',' CSV HEADER;
```

### Import from CSV files

In the following example, the data exported in the previous examples are imported in the `users` table.

```plpgsql
yugabyte=# COPY users FROM '/home/yuga/Desktop/users.txt.sql' DELIMITER ',' CSV HEADER;
```

### Import with skipping rows

Assume we ran the command one time, and it failed in the middle, as if the server had crashed.
Since we use `ROWS_PER_TRANSACTION=5000`, we can resume importing at multiples of 5000:

```plpgsql
yugabyte=# COPY users FROM '/home/yuga/Desktop/users.txt.sql' WITH (FORMAT CSV,
HEADER, DELIMITER ',', ROWS_PER_TRANSACTION 5000, SKIP 50000);
```


### Import with replacing rows

If duplicate rows exist in the database, we can use `REPLACE` to upsert new rows:

```plpgsql
yugabyte=# COPY users FROM '/home/yuga/Desktop/users.txt.sql' WITH (FORMAT CSV,
HEADER, DELIMITER ',', REPLACE);
```

### Import with disabling foreign key checks

If we're certain that rows referred by foreign keys already exist, we can disable checking for them to make the import faster:

```plpgsql
yugabyte=# COPY users FROM '/home/yuga/Desktop/users.txt.sql' WITH (FORMAT CSV,
HEADER, DELIMITER ',', DISABLE_FK_CHECK);
```

### Using all options

In the following example, we use all of the `COPY` command's options:

```plpgsql
yugabyte=# COPY users FROM '/home/yuga/Desktop/users.txt.sql' WITH (FORMAT CSV,
HEADER, DELIMITER ',', ROWS_PER_TRANSACTION 5000, DISABLE_FK_CHECK, REPLACE, SKIP 50);
```

For COPY operation examples using the `pg_stat_progress_copy` view, refer to [View COPY status with pg_stat_progress_copy](../../../../../explore/query-1-performance/pg-stat-progress-copy/).
