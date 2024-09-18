---
title: Explore Yugabyte SQL
linkTitle: Explore distributed SQL
description: Use distributed SQL to explore core features of YugabyteDB.
headcontent:
menu:
  preview_yugabyte-cloud:
    identifier: qs-explore-1-ysql
    parent: yugabytedb-managed
    params:
      hide: true
type: docs
---

After [creating a Sandbox cluster](../../cloud-basics/create-clusters/create-clusters-free/) and [connecting to the cluster](../../cloud-connect/connect-cloud-shell/) using Cloud Shell, you can start exploring YugabyteDB's PostgreSQL-compatible, fully-relational Yugabyte SQL API.

When you connect to your cluster using Cloud Shell with the YSQL API, the shell window incorporates a quick start guide, with a series of pre-built queries for you to run.

{{< youtube id="01owTbmSDe8" title="Explore distributed SQL in YugabyteDB Aeon" >}}

## Run the tutorial

After entering your password, do the following to start the tutorial (creating a database is optional):

![Run the quick start tutorial](/images/yb-cloud/cloud-shell-tutorial.gif)

### Create a database

While optional, it's good practice to create separate databases for different applications.

To create a database, enter the following command:

```sql
yugabyte=> CREATE DATABASE yb_demo;
```

Next, connect to the new database using the ysqlsh `\c` meta-command:

```sql
yugabyte=> \c yb_demo;
```

### Create tables and insert data

Begin the tutorial by selecting the steps in the left navigation panel, then clicking the **Run** button for the corresponding SQL statements. The tutorial starts with the following tasks:

1. Create a Table.\
\
    The tutorial database includes two tables: `dept` for Departments, and `emp` for Employees. The employees table references the departments table through a foreign key constraint. The employees table also references itself through a foreign key constraint to ensure that an employee's manager is in turn an employee themselves. The table uses these constraints to ensure integrity of data, such as uniqueness and validity of email addresses.
1. Insert Data.\
\
    Data is added to the tables using multi-value inserts to reduce client-server round trips.

After you create the tables and insert the data, click **Let's get started with the tutorials** to begin the tutorial scenarios.

### Quick start scenarios

The quick start includes the following scenarios:

| Scenario | Description |
| :--- | :--- |
| **Basics** |
| SQL Updates | Update the salary of all employees who are not managers and display the new results. |
| Join | List all employees earning more than their managers using a self-join query. |
| Prepared Statements | Get the salary for an employee using a prepared statement. |
| Indexes | Create and analyze an index on the fly. |
| Recursive Queries | List the manager hierarchy using a recursive query. |
| **Built-in Functions** |
| Window Functions | Compare employee hiring time interval by department using an analytical window function. |
| Regexp Matching | List all employees matching Gmail and org email domains using regular expressions for text pattern matching. |
| Arithmetic Date Intervals | Find employees with overlapping evaluation periods using arithmetic date intervals. |
| Crosstab view | Display the sum of salary per job and department as a cross table. |
| ntile Function | Split e-mails into 3 groups and format them. |
| **Advanced Features** |
| GIN Index | Query employee skills using a GIN index on a JSON document. |
| Text Search Index | Create a GIN text search index on the description column and query for words. |
| Stored Procedures | Transfer a commission from one employee to another using a stored procedure. |
| Triggers | Record the last update time of each row automatically. |
| Materialized Views | Pre-compute analytics for reporting using a materialized view. |

To run this tutorial from your desktop shell, refer to [Explore Yugabyte SQL](../../../quick-start/explore/ysql/) in the Core Quick Start.

## Next step

[Build an application](../../../tutorials/build-apps/)
