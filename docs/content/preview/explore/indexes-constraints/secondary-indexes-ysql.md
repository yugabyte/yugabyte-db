---
title: Secondary indexes in YugabyteDB YSQL
headerTitle: Secondary indexes
linkTitle: Secondary indexes
description: Overview of Secondary indexes in YSQL
headContent: Explore secondary indexes in YugabyteDB using YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  preview:
    identifier: secondary-indexes-ysql
    parent: explore-indexes-constraints
    weight: 210
aliases:
  - /preview/explore/ysql-language-features/indexes-1/
  - /preview/explore/indexes-constraints/secondary-indexes/
type: docs
---
<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../secondary-indexes-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../secondary-indexes-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The use of indexes can enhance database performance by enabling the database server to find rows faster. You can create, drop, and list indexes, as well as use indexes on expressions.

## Create indexes

You can create indexes in YSQL using the `CREATE INDEX` statement using the following syntax:

```sql
CREATE INDEX index_name ON table_name(column_list);
```

*column_list* represents a column or a comma-separated list of several columns to be stored in the index. An index created for more than one column is called a composite index.

You can also create a functional index in YSQL, in which case you would replace any element of *column_list* with an expression. For more information, see [Expression indexes](../../../explore/indexes-constraints/expression-index-ysql/).

YSQL currently supports index access methods `lsm` (log-structured merge-tree) and `ybgin`. These indexes are based on YugabyteDB's DocDB storage and are similar in functionality to PostgreSQL's `btree` and `gin` indexes, respectively. The index access method can be specified with `USING <access_method_name>` after *table_name*. By default, `lsm` is chosen.

For more information on `ybgin`, see [Generalized inverted index](../../../explore/indexes-constraints/gin/).

You can apply sort order on the indexed columns as `HASH` (default option for the first column), `ASC` (default option for the second and subsequent columns), as well as `DESC`.

For examples, see [Unique index with HASH column ordering](../../../api/ysql/the-sql-language/statements/ddl_create_index/#unique-index-with-hash-column-ordering) and [ASC ordered index](../../../api/ysql/the-sql-language/statements/ddl_create_index/#asc-ordered-index).

## List indexes and verify the query plan

YSQL inherits all the functionality of the PostgreSQL `pg_indexes` view that allows you to retrieve a list of all indexes in the database as well as detailed information about every index.

```sql
SELECT indexname, indexdef FROM pg_indexes WHERE tablename = 'your_table_name';
```

For details, see [pg_indexes](https://www.postgresql.org/docs/12/view-pg-indexes.html) in the PostgreSQL documentation.

For YCQL, you can use the [DESCRIBE](../../../admin/ycqlsh/#describe) INDEX command to check the indexes as follows:

```sql
DESCRIBE INDEX <index_name>;
```

You can also use the `EXPLAIN` statement to check if a query uses an index and determine the query plan before execution.

For more information, see [EXPLAIN statement in YSQL](../../../api/ysql/the-sql-language/statements/perf_explain/).

## Remove indexes

You can remove one or more existing indexes using the `DROP INDEX` statement in YSQL using the following syntax:

```sql
DROP INDEX index_name1, index_name2, index_name3, ... ;
```

For additional information, see [DROP INDEX YCQL API](../../../api/ycql/ddl_drop_index/).

## Example scenario using YSQL

{{% explore-setup-single %}}

Suppose you work with a database that includes the following table populated with data:

```sql
CREATE TABLE employees (
  employee_no integer,
  name text,
  department text
);
```

```sql
INSERT INTO employees VALUES
(1221, 'John Smith', 'Marketing'),
(1222, 'Bette Davis', 'Sales'),
(1223, 'Lucille Ball', 'Operations');
```

The following example shows a query that finds employees working in Operations department:

```sql
SELECT * FROM employees WHERE department = 'Operations';
```

To process the preceding query, the whole `employees` table needs to be scanned. For large organizations, this might take a significant amount of time.

To speed up the process, you create an index for the department column, as follows:

```sql
CREATE INDEX index_employees_department
  ON employees(department);
```

The following example executes the query after the index has been applied to `department` and uses the `EXPLAIN` statement to prove that the index participated in the processing of the query:

```sql
EXPLAIN SELECT * FROM employees WHERE department = 'Operations';
```

Following is the output produced by the preceding example:

```output
QUERY PLAN
-----------------------------------------------------------------------------------
Index Scan using index_employees_department on employees (cost=0.00..5.22 rows=10 width=68)
Index Cond: (department = 'Operations'::text)
```

For additional information, see:

- [CREATE INDEX YSQL API](../../../api/ysql/the-sql-language/statements/ddl_create_index/)
- [CREATE INDEX YCQL API](../../../api/ycql/ddl_create_index/)

To remove the index `index_employees_department`, use the following command:

```sql
DROP INDEX index_employees_department;
```

## Learn more

- [Benefits of Index-only scan](https://www.yugabyte.com/blog/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/)
- Blog on [Pushdown #3: Filtering using index predicates](https://www.yugabyte.com/blog/5-query-pushdowns-for-distributed-sql-and-how-they-differ-from-a-traditional-rdbms/) discusses the performance boost of distributed SQL queries using indexes.
