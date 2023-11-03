---
title: Connect an app
linkTitle: Connect an app
description: Python drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.14:
    identifier: yugabyte-psycopg2-driver
    parent: python-drivers
    weight: 400
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../yugabyte-psycopg2" class="nav-link active">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      YugabyteDB Psycopg2
    </a>
  </li>

  <li >
    <a href="../postgres-psycopg2" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Psycopg2
    </a>
  </li>

</ul>

The [Yugabyte Psycopg2 smart driver](https://github.com/yugabyte/psycopg2) is a distributed Python driver for [YSQL](../../../api/ysql/), built on the [PostgreSQL psycopg2 driver](https://github.com/psycopg/psycopg2). Although the upstream PostgreSQL psycopg2 driver works with YugabyteDB, the Yugabyte driver is cluster- and topology-aware, and eliminates the need for external load balancers.

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in the [Build an application](../../../quick-start/build-apps/python/ysql-psycopg2/) page under the Quick start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Python application development using the YugabyteDB Psycopg2 driver.

### Step 1: Add the YugabyteDB driver dependency

Building Psycopg requires a few prerequisites (a C compiler and some development packages). Check the [installation instructions](https://www.psycopg.org/docs/install.html#build-prerequisites) and [the FAQ](https://www.psycopg.org/docs/faq.html#faq-compile) for details.

The YugabyteDB Psycopg2 requires PostgreSQL version 11 or above (preferably 14).

After you've installed the prerequisites, install psycopg2-yugabytedb like any other Python package, using pip to download it from [PyPI](https://pypi.org/project/psycopg2-yugabytedb/):

```sh
$ pip install psycopg2-yugabytedb
```

Or, you can use the setup.py script if you've downloaded the source package locally:

```sh
$ python setup.py build
$ sudo python setup.py install
```

### Step 2: Connect to your cluster

Python applications can connect to and query the YugabyteDB database. First, import the psycopg2 package.

```python
import psycopg2
```

You can provide the connection details in one of the following ways:

- Connection string:

  ```python
  "dbname=database_name host=hostname port=port user=username  password=password load_balance=true"
  ```

- Connection dictionary:

  ```python
  user = 'username', password='xxx', host = 'hostname', port = 'port', dbname = 'database_name', load_balance='True'
  ```

The following is an example URL for connecting to YugabyteDB.

```python
conn = psycopg2.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte', load_balance='True')
```

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| host | Hostname of the YugabyteDB instance | localhost |
| port | Listen port for YSQL | 5433 |
| database/dbname | Database name | yugabyte |
| user | User connecting to the database | yugabyte |
| password | User password | yugabyte |
| load_balance | Enables uniform load balancing | false |

#### Use SSL

Use the following example URL for connecting to a YugabyteDB cluster with SSL enabled:

```python
conn = psycopg2.connect("host=<hostname> port=5433 dbname=yugabyte user=<username> password=<password> load_balance=true sslmode=verify-full sslrootcert=/Users/my-user/Downloads/root.crt")
```

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| sslmode | SSL mode | prefer |
| sslrootcert | path to the root certificate on your computer | ~/.postgresql/ |

If you have created a cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/), [follow the steps](/preview/yugabyte-cloud/cloud-connect/connect-applications/) to obtain the cluster connection parameters and SSL Root certificate.

### Step 3: Query the YugabyteDB cluster from your application

1. Create a new Python file called `QuickStartApp.py` in the base package directory of your project.

1. Copy the following sample code to set up tables and query the table contents. Replace the connection string `connString` with the cluster credentials and SSL certificate, if required.

   ```python
   import psycopg2

   # Create the database connection.

   connString = "host=127.0.0.1 port=5433 dbname=yugabyte user=yugabyte password=yugabyte     load_balance=True"

   conn = psycopg2.connect(connString)

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

## Next steps

- Learn how to build Python applications using [Django](../../../drivers-orms/python/django/).
- Learn how to build Python applications using [SQLAlchemy](../../../drivers-orms/python/sqlalchemy/).
- Learn more about [fundamentals](../../../reference/drivers/python/yugabyte-psycopg2-reference/#fundamentals) of the YugabyteDB Psycopg2 driver.
