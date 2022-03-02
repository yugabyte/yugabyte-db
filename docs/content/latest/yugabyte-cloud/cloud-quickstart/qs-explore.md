---
title: Explore Yugabyte SQL
linkTitle: Explore distributed SQL
description: Use distributed SQL to explore core features of YugabteDB.
headcontent:
image: /images/section_icons/index/quick_start.png
menu:
  latest:
    identifier: qs-explore-1-ysql
    parent: cloud-quickstart
    weight: 400
type: page
isTocNested: true
showAsideToc: true
---

After [creating a free cluster](../qs-add/) and [connecting to the cluster](../qs-connect/) using Cloud Shell, you can start exploring YugabyteDB's PostgreSQL-compatible, fully-relational Yugabyte SQL API.

## Explore YugabyteDB

When you connect to your cluster using Cloud Shell with the YSQL API, the shell window incorporates a quick start guide, with a series of pre-built queries for you to run.

To start, create a database by entering the following command:

```sql
yugabyte=# CREATE DATABASE yb_demo;
```

Next, connect to the new database using the ysqlsh `\c` meta-command:

```sql
yugabyte=# \c yb_demo;
```

Begin the tutorial by selecting the steps in the left navigation panel. The tutorial starts with the following tasks:

1. Create a Table.
1. Insert Data.

The tutorial database includes two tables: `dept` for Departments, and `emp` for Employees.

After you create the tables and insert the data, you can begin the tutorial scenarios. The quick start includes the following:

| Scenario | Description |
| :--- | :--- |
| Self Join | List all employees earning more than their managers using a self-join query. |
| With Recursive | List the manager hierarchy using a recursive query. |
| LAG Window Function | Compare employee hiring time interval by department using an analytical window function. |
| CROSSTABVIEW | Display the sum of salary per job and department using a cross tab pivot query. |
| Regexp | List all employees matching Gmail and org email domains using text pattern matching with regexp. |
| GIN Index | Query employee skills using a GIN index on a JSON document. |
| Text Search Index | Create a GIN text search index on the description column and query for words. |
| Arithmetic Date Intervals | Find employees with overlapping evaluation periods using arithmetic date intervals. |
| SQL Update | Update the salary of all employees who are not managers and display the new results. |
| Prepared Statement | Get the salary for an employee using a prepared statement. |
| Stored Procedure | Transfer a commission from one employee to another. |
| Trigger | Record the last update time of each row automatically. |
| Create Index | Create and analyze an index on the fly. |

## Next step

[Build an application](../cloud-build-apps/)
