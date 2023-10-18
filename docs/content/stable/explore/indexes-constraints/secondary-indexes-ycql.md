---
title: Secondary indexes in YugabyteDB YCQL
headerTitle: Secondary indexes
linkTitle: Secondary indexes
description: Overview of Secondary indexes in YCQL
headContent: Explore secondary indexes in YugabyteDB using YCQL
image: /images/section_icons/secure/create-roles.png
menu:
  stable:
    identifier: secondary-indexes-ycql
    parent: explore-indexes-constraints
    weight: 220
type: docs
---
<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../secondary-indexes-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../secondary-indexes-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The use of indexes can enhance database performance by enabling the database server to find rows faster. You can create, drop, and list indexes in YCQL.

## Create indexes

You can create indexes in YCQL using the `CREATE INDEX` statement using the following syntax:

```sql
CREATE INDEX index_name ON table_name(column_list);
```

YCQL supports [Unique](../../../explore/indexes-constraints/unique-index-ycql/), [Partial](../../../explore/indexes-constraints/partial-index-ycql/), and [Covering](../../../explore/indexes-constraints/covering-index-ycql/) secondary indexes.

For additional information on creating indexes, see [CREATE INDEX](../../../api/ycql/ddl_create_index/).

## List indexes and verify the query plan

You can use the [DESCRIBE INDEX](../../../admin/ycqlsh/#describe) command to check the indexes as follows:

```cql
DESCRIBE INDEX <index_name>
```

For additional information, see [DESCRIBE INDEX](../../../admin/ycqlsh/#describe).

You can also use the `EXPLAIN` statement to check if a query uses an index and determine the query plan before execution.

For more information, see [EXPLAIN statement in YCQL](../../../api/ycql/explain/).

## Remove indexes

You can remove an index using the `DROP INDEX` statement in YCQL using the following syntax:

```sql
DROP INDEX index_name;
```

For additional information, see [DROP INDEX](../../../api/ycql/ddl_drop_index/).

## Example scenario using YCQL

{{% explore-setup-single %}}

Suppose you work with a database that includes the following table populated with data:

```cql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text
)
WITH TRANSACTIONS = {'enabled':'true'};
```

```cql
INSERT INTO employees(employee_no, name,department) VALUES(1221, 'John Smith', 'Marketing');
INSERT INTO employees(employee_no, name,department) VALUES(1222, 'Bette Davis', 'Sales');
INSERT INTO employees(employee_no, name,department) VALUES(1223, 'Lucille Ball', 'Operations');
```

The following example shows a query that finds employees working in Operations:

```cql
SELECT * FROM employees WHERE department = 'Operations';
```

To process the preceding query, the whole `employees` table needs to be scanned and all the shards have to be accessed. For large organizations, this will take a significant amount of time. You can confirm this using the following `EXPLAIN` statement which indicates `seq scan` on the table.

```cql
EXPLAIN SELECT * FROM employees WHERE department = 'Operations';
```

```output
 QUERY PLAN
---------------------------------------
 Seq Scan on employees
   Filter: (department = 'Operations')
```

To speed up the process, you create an index for the department column, as follows:

```cql
CREATE INDEX index_employees_department ON employees(department);
```

The following example executes the query after the index has been applied to `department` and uses the `EXPLAIN` statement to prove that the index is used during query execution:

```cql
EXPLAIN SELECT * FROM employees WHERE department = 'Operations';
```

Following is the output produced by the preceding example:

```output
 QUERY PLAN
--------------------------------------------------------------------
 Index Scan using docs.index_employees_department on docs.employees
   Key Conditions: (department = 'Operations')
```

To remove the index `index_employees_department`, use the following command:

```cql
DROP INDEX index_employees_department;
```

## Learn more

- [Secondary indexes with JSONB](../secondary-indexes-with-jsonb-ycql/)
