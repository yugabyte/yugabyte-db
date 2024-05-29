---
title: Secondary indexes in YugabyteDB YSQL
headerTitle: Secondary indexes
linkTitle: Secondary indexes
description: Overview of Secondary indexes in YSQL
headContent: Explore secondary indexes in YugabyteDB using YSQL
menu:
  preview:
    identifier: secondary-indexes-ysql
    parent: explore-indexes-constraints-ysql
    weight: 210
aliases:
  - /preview/explore/ysql-language-features/indexes-1/
  - /preview/explore/indexes-constraints/secondary-indexes/
type: docs
---

Using indexes enhances database performance by enabling the database server to find rows faster. You can create, drop, and list indexes, as well as use indexes on expressions.

{{<note>}}
In YugabyteDB indexes are global and are implemented just like tables. They are split into tablets and distributed across the different nodes in the cluster. The sharding of indexes is based on the primary key of the index and is independent of how the main table is sharded/distributed. Indexes are not colocated with the base table.
{{</note>}}

## Create indexes

You can create indexes in YSQL using the `CREATE INDEX` statement using the following syntax:

```sql
CREATE INDEX index_name ON table_name(column_list);
```

*column_list* represents a column or a comma-separated list of several columns to be stored in the index. An index created for more than one column is called a composite index (multi-column index).

For more information, see [CREATE INDEX](../../../../api/ysql/the-sql-language/statements/ddl_create_index/).

[Multi-column indexes](#multi-column-index) can be beneficial in situations where queries are searching in more than a single column.

You can also create a functional index in YSQL, in which case you would replace any element of *column_list* with an expression. For more information, see [Expression indexes](../expression-index-ysql/).

YSQL currently supports index access methods `lsm` (log-structured merge-tree) and `ybgin`. These indexes are based on YugabyteDB's DocDB storage and are similar in functionality to PostgreSQL's `btree` and `gin` indexes, respectively. The index access method can be specified with `USING <access_method_name>` after *table_name*. By default, `lsm` is chosen.

For more information on `ybgin`, see [Generalized inverted index](../gin/).

You can apply sort order on the indexed columns as `HASH` (default option for the first column), `ASC` (default option for the second and subsequent columns), as well as `DESC`.

For examples, see [Unique index with HASH column ordering](../../../../api/ysql/the-sql-language/statements/ddl_create_index/#unique-index-with-hash-column-ordering) and [ASC ordered index](../../../../api/ysql/the-sql-language/statements/ddl_create_index/#asc-ordered-index).

## List indexes and verify the query plan

YSQL inherits all the functionality of the PostgreSQL `pg_indexes` view that allows you to retrieve a list of all indexes in the database as well as detailed information about every index.

```sql
SELECT indexname, indexdef FROM pg_indexes WHERE tablename = 'your_table_name';
```

For details, see [pg_indexes](https://www.postgresql.org/docs/12/view-pg-indexes.html) in the PostgreSQL documentation.

You can also use the `EXPLAIN` statement to check if a query uses an index and determine the query plan before execution.

For more information, see [EXPLAIN](../../../../api/ysql/the-sql-language/statements/perf_explain/).

## Remove indexes

You can remove one or more existing indexes using the `DROP INDEX` statement in YSQL using the following syntax:

```sql
DROP INDEX index_name1, index_name2, index_name3, ... ;
```

For more information, see [DROP INDEX](../../../../api/ysql/the-sql-language/statements/ddl_drop_index/).

## Example

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

To remove the index `index_employees_department`, use the following command:

```sql
DROP INDEX index_employees_department;
```

## Multi-column index

To add a multi-column index during table creation, you can use the following syntax:

```sql
CREATE TABLE table_name (
    col1 data_type PRIMARY KEY,
    col2 data_type,
    col3 data_type,
    col3 data_type,
    INDEX index_name (col2,col3,col4)
);
```

To add a multi-column index to an existing table, you can use the following syntax:

```sql
CREATE INDEX index_name ON table_name(col2,col3,col4);
```

The column order is very important when you create a multi-column index in YSQL because of the structure in which the index is stored. As such, these indexes have a hierarchical order from left to right. So, for the preceding syntaxes, you can perform search using the following column combinations:

```sql
(col2)
(col2,col3)
(col2,col3,col4)
```

A column combination like (col2,col4) cannot be used to search or query a table.

## Multi-column example

{{% explore-setup-single %}}

This example uses the `employees` table from the [Northwind sample database](../../../../sample-data/northwind/#install-the-northwind-sample-database).

View the contents of the `employees` table:

```sql
SELECT * FROM employees LIMIT 2;
```

```output
employee_id  | last_name | first_name |        title         | title_of_courtesy | birth_date | hire_date  |           address           |  city   | region | postal_code | country |   home_phone   | extension | photo |                                                                                                           notes                                                                                                            | reports_to |              photo_path
-------------+-----------+------------+----------------------+-------------------+------------+------------+-----------------------------+---------+--------+-------------+---------+----------------+-----------+-------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+------------+--------------------------------------
           4 | Peacock   | Margaret   | Sales Representative | Mrs.              | 1937-09-19 | 1993-05-03 | 4110 Old Redmond Rd.        | Redmond | WA     | 98052       | USA     | (206) 555-8122 | 5176      | \x    | Margaret holds a BA in English literature from Concordia College (1958) and an MA from the American Institute of Culinary Arts (1966).  She was assigned to the London office temporarily from July through November 1992. |          2 | http://accweb/emmployees/peacock.bmp
           1 | Davolio   | Nancy      | Sales Representative | Ms.               | 1948-12-08 | 1992-05-01 | 507 - 20th Ave. E.\nApt. 2A | Seattle | WA     | 98122       | USA     | (206) 555-9857 | 5467      | \x    | Education includes a BA in psychology from Colorado State University in 1970.  She also completed The Art of the Cold Call.  Nancy is a member of Toastmasters International.                                              |          2 | http://accweb/emmployees/davolio.bmp
(2 rows)

```

Suppose you want to query the subset of employees by their first and last names. The query plan using the `EXPLAIN` statement would look like the following:

```sql
EXPLAIN SELECT * FROM employees WHERE last_name='Davolio' AND first_name='Nancy';
```

```output
                                            QUERY PLAN
---------------------------------------------------------------------------------------------------
 Seq Scan on employees  (cost=0.00..105.00 rows=1000 width=1240)
   Storage Filter: (((last_name)::text = 'Davolio'::text) AND ((first_name)::text = 'Nancy'::text))
(2 rows)
```

Without creating a multi-column index, querying the `employees` table with the `WHERE` clause scans all the 1000 rows sequentially. Creating an index limits the number of rows to be scanned for the same query.

Create a multi-column index on the columns `last_name` and `first_name` from the `employees` table as follows:

```sql
CREATE INDEX index_names ON employees(last_name, first_name);
```

Verify with the `EXPLAIN` statement that the number of rows is significantly less compared to the original query plan.

```sql
EXPLAIN SELECT * FROM employees WHERE last_name='Davolio' AND first_name='Nancy';
```

```output
                                           QUERY PLAN
------------------------------------------------------------------------------------------------
 Index Scan using index_names on employees  (cost=0.00..5.25 rows=10 width=1240)
   Index Cond: (((last_name)::text = 'Davolio'::text) AND ((first_name)::text = 'Nancy'::text))
(2 rows)
```

With the index `index_names`, you can also search for employees by their last names as follows:

```sql
EXPLAIN SELECT * FROM employees WHERE last_name='Davolio';
```

```output
                                    QUERY PLAN
-----------------------------------------------------------------------------------
 Index Scan using index_names on employees  (cost=0.00..16.25 rows=100 width=1240)
   Index Cond: ((last_name)::text = 'Davolio'::text)
(2 rows)
```

## Learn more

- [Benefits of Index-only scan](https://www.yugabyte.com/blog/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/)
- Blog on [Pushdown #3: Filtering using index predicates](https://www.yugabyte.com/blog/5-query-pushdowns-for-distributed-sql-and-how-they-differ-from-a-traditional-rdbms/) discusses the performance boost of distributed SQL queries using indexes.
- [How To Design Distributed Indexes for Optimal Query Performance](https://www.yugabyte.com/blog/design-indexes-query-performance-distributed-database/)
