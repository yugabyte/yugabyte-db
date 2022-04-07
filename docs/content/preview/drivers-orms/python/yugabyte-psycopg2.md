---
title: Python Drivers
linkTitle: Python Drivers
description: Python Drivers for YSQL
headcontent: Python Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: Python Drivers
    identifier: yugabyte-psycopg2-driver
    parent: python-drivers
    weight: 400
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/preview/drivers-orms/python/yugabyte-psycopg2" class="nav-link active">
      <i class="icon-java-bold" aria-hidden="true"></i>
      YugabyteDB Psycopg2
    </a>
  </li>

  <li >
    <a href="/preview/drivers-orms/python/postgres-psycopg2" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Psycopg2
    </a>
  </li>

</ul>

The `YugabyteDB Smart Psycopg2 Driver` is cluster- and topology-aware.

This page provides details for getting started with `YugabyteDB Psycopg2 Driver` for connecting to YugabyteDB YSQL API.

[Yugabyte Psycopg2 driver](https://github.com/yugabyte/psycopg2) is a distributed python driver for [YSQL](/preview/api/ysql/) built on the [PostgreSQL psycopg2 driver](https://github.com/psycopg/psycopg2).
Although the upstream PostgreSQL psycopg2 driver works with YugabyteDB, the Yugabyte driver enhances YugabyteDB by eliminating the need for external load balancers.

## CRUD Operations with YugabyteDB Psycopg2 Driver

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using
the steps in the [Build an application](/preview/quick-start/build-apps/python/ysql-psycopg2/) page under the Quick start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Python application development using the YugabyteDB Psycopg2 driver.

### Step 1: Add the YugabyteDB Driver Dependency

<!-- TODO: After publishing the driver -->

### Step 2: Connect to your Cluster

Python applications can connect to and query the YugabyteDB database. First, import the psycopg2 package.

```python
import psycopg2
```

The Connection details can be provided as a string or a dictionary.
Connection String

```python
"dbname=database_name host=hostname port=port user=username  password=password load_balance=true"
```

Connection Dictionary

```python
user = 'username', password='xxx', host = 'hostname', port = 'port', dbname = 'database_name', load_balance='True'
```

Example URL for connecting to YugabyteDB can be seen below.

```python
conn = psycopg2.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte', load_balance='True')
```

| Params | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database/dbname | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| load-balance | enables uniform load balancing | false

Example URL for connecting to YugabyteDB cluster enabled with on the wire SSL encryption.

```python
conn = psycopg2.connect("host=<hostname> port=5433 dbname=yugabyte user=<username> password=<password> load_balance=true sslmode=verify-full sslrootcert=/Users/my-user/Downloads/root.crt")
```

| Params | Description | Default |
| :---------- | :---------- | :------ |
| sslmode | SSL mode  | prefer
| sslrootcert | path to the root certificate on your computer | ~/.postgresql/

If you have created a cluster on [Yugabyte Cloud](https://www.yugabyte.com/cloud/), [follow the steps](/preview/yugabyte-cloud/cloud-connect/connect-applications/) to obtain the cluster connection parameters and SSL Root certificate.

### Step 3: Query the YugabyteDB Cluster from Your Application

Create a new Python file called `QuickStartApp.py` in the base package directory of your project. Copy the sample code below to set up tables and query the table contents. Replace the connection string `yburl` with the cluster credentials and SSL certificate, if required.

```python
import psycopg2

# Create the database connection.

yburl = "host=127.0.0.1 port=5433 dbname=yugabyte user=yugabyte password=yugabyte load_balance=True"

conn = psycopg2.connect(yburl)

# Open a cursor to perform database operations.
# The default mode for psycopg2 is "autocommit=false".

conn.set_session(autocommit=True)
cur = conn.cursor()

# Create the table. (It might preexist.)

cur.execute(
  """
  DROP TABLE IF EXISTS employee
  """)

cur.execute(
  """
  CREATE TABLE employee (id int PRIMARY KEY,
                         name varchar,
                         age int,
                         language varchar)
  """)
print("Created table employee")
cur.close()

# Take advantage of ordinary, transactional behavior for DMLs.

conn.set_session(autocommit=False)
cur = conn.cursor()

# Insert a row.

cur.execute("INSERT INTO employee (id, name, age, language) VALUES (%s, %s, %s, %s)",
            (1, 'John', 35, 'Python'))
print("Inserted (id, name, age, language) = (1, 'John', 35, 'Python')")

# Query the row.

cur.execute("SELECT name, age, language FROM employee WHERE id = 1")
row = cur.fetchone()
print("Query returned: %s, %s, %s" % (row[0], row[1], row[2]))

# Commit and close down.

conn.commit()
cur.close()
conn.close()
```

When you run the `QuickStartApp.py` project, you should see output similar to the following:

```text
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
Query returned: John, 35, Python
```

If there is no output or you get an error, verify the parameters included in the connection string.

After completing these steps, you should have a working Python app that uses Psycopg2 for connecting to your cluster, setup tables, run query and print out results.

## Next Steps

- Learn how to build Python applications using [Django](/preview/drivers-orms/python/django/)
- Learn how to build Python applications using [SQLAlchemy](/preview/drivers-orms/python/sqlalchemy/)
- Learn more about [fundamentals](../../../reference/drivers/python/yugabyte-psycopg2-reference/) of the YugabyteDB Psycopg2 Driver
