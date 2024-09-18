---
title: Northwind sample database
headerTitle: Northwind sample database
linkTitle: Northwind
description: Use the Northwind sample database to explore and learn YugabyteDB.
menu:
  preview:
    identifier: northwind
    parent: sample-data
    weight: 200
type: docs
---

Install the PostgreSQL-compatible version of the Northwind dataset on the YugabyteDB distributed SQL database.

You can install and use the Northwind sample database using:

- A local installation of YugabyteDB. To install YugabyteDB, refer to [Quick Start](../../quick-start/).
- Using cloud shell or a client shell to connect to a cluster in YugabyteDB Aeon. Refer to [Connect to clusters in YugabyteDB Aeon](../../yugabyte-cloud/cloud-connect/). To get started with YugabyteDB Aeon, refer to [Quick Start](../../yugabyte-cloud/cloud-quickstart/).

In either case, you use the YugabyteDB SQL shell ([ysqlsh](../../admin/ysqlsh/)) CLI to interact with YugabyteDB using [YSQL](../../api/ysql/).

## About the Northwind sample database

The Northwind database is a sample database that was originally created by Microsoft and used as the basis for their tutorials in a variety of database products for decades. The Northwind database contains the sales data for a fictitious company called “Northwind Traders,” which imports and exports specialty foods from around the world. The Northwind database is an excellent tutorial schema for a small-business ERP, with customers, orders, inventory, purchasing, suppliers, shipping, employees, and single-entry accounting. The Northwind database has since been ported to a variety of non-Microsoft databases, including PostgreSQL.

The Northwind dataset includes sample data for the following.

- **Suppliers**: Suppliers and vendors of Northwind
- **Customers**: Customers who buy products from Northwind
- **Employees**: Employee details of Northwind traders
- **Products**: Product information
- **Shippers**: The details of the shippers who ship the products from the traders to the end-customers
- **Orders and Order_Details**: Sales Order transactions taking place between the customers & the company

The Northwind sample database includes 14 tables and the table relationships are showcased in the following entity relationship diagram.

![Northwind ER diagram](/images/sample-data/northwind/northwind-er-diagram.png)

## Install the Northwind sample database

The Northwind SQL scripts reside in the `share` folder of your YugabyteDB or client shell installation. They can also be found in the [`sample` directory of the YugabyteDB GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/sample). The following files will be used for this exercise:

- [northwind_ddl.sql](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/northwind_ddl.sql) — Creates tables and other database objects
- [northwind_data.sql](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/northwind_data.sql) — Loads the sample data

Follow the steps here to install the Northwind sample database.

### Open the YSQL shell

If you are using a local installation of YugabyteDB, run the `ysqlsh` command from the `yugabyte` root directory.

```sh
$ ./bin/ysqlsh
```

If you are connecting to YugabyteDB Aeon, open the [ysqlsh cloud shell](../../yugabyte-cloud/cloud-connect/connect-cloud-shell/), or [run the YSQL connection string](../../yugabyte-cloud/cloud-connect/connect-client-shell/#ysqlsh) for your cluster from the `yugabyte-client` bin directory.

### Create the Northwind database

To create the Northwind database, run the following `CREATE DATABASE` statement.

```plpgsql
CREATE DATABASE northwind;
```

Confirm that you have the Northwind database by listing the databases on your cluster.

```plpgsql
yugabyte=# \l
```

Connect to the Northwind database.

```plpgsql
yugabyte=# \c northwind
```

### Build the tables and objects

To build the tables and database objects, execute the `northwind_ddl.sql` SQL script.

```plpgsql
northwind=# \i share/northwind_ddl.sql
```

You can verify that all 14 tables have been created by running the `\d` command.

```plpgsql
northwind=# \d
```

```output
                List of relations
 Schema |          Name          | Type  | Owner
--------+------------------------+-------+-------
 public | categories             | table | admin
 public | customer_customer_demo | table | admin
 public | customer_demographics  | table | admin
 public | customers              | table | admin
 public | employee_territories   | table | admin
 public | employees              | table | admin
 public | order_details          | table | admin
 public | orders                 | table | admin
 public | products               | table | admin
 public | region                 | table | admin
 public | shippers               | table | admin
 public | suppliers              | table | admin
 public | territories            | table | admin
 public | us_states              | table | admin
(14 rows)
```

### Load the sample data

To load the `northwind` database with sample data, run the `\i` command to execute commands in the `northwind_data.sql` file.

```plpgsql
northwind=# \i share/northwind_data.sql
```

To verify that you have some data to work with, you can run a simple SELECT statement to pull data from the `customers` table.

```plpgsql
northwind=# SELECT * FROM customers LIMIT 2;
```

```output
 customer_id |       company_name        | contact_name |    contact_title    |      address       |   city    | region | postal_code | country |     phone     |     fax
-------------+---------------------------+--------------+---------------------+--------------------+-----------+--------+-------------+---------+---------------+-------------
 FAMIA       | Familia Arquibaldo        | Aria Cruz    | Marketing Assistant | Rua Orós, 92       | Sao Paulo | SP     | 05442-030   | Brazil  | (11) 555-9857 |
 VINET       | Vins et alcools Chevalier | Paul Henriot | Accounting Manager  | 59 rue de l'Abbaye | Reims     |        | 51100       | France  | 26.47.15.10   | 26.47.15.11
(2 rows)
```

## Explore the Northwind database

That’s it! You are now ready to start exploring the Northwind database and YugabyteDB features using the command line or your favorite PostgreSQL tool.
