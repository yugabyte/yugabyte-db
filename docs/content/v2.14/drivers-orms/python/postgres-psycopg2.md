---
title: Connect an app
linkTitle: Connect an app
description: Python drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.14:
    identifier: postgres-psycopg2-driver
    parent: python-drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../yugabyte-psycopg2/" class="nav-link">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      YugabyteDB Psycopg2
    </a>
  </li>

  <li >
    <a href="../postgres-psycopg2/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Psycopg2
    </a>
  </li>

</ul>

Psycopg is the most popular PostgreSQL database adapter for Python. Its main features are the complete implementation of the Python DB API 2.0 specification and the thread safety (several threads can share the same connection). YugabyteDB has full support for [Psycopg2](https://www.psycopg.org/).

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in the [Build an application](../../../quick-start/build-apps/python/ysql-psycopg2/) page under the Quick start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Python application development using the PostgreSQL Psycopg2 driver.

### Step 1: Download the driver dependency

Building Psycopg requires a few prerequisites (a C compiler, some development packages). Refer to [Installation](https://www.psycopg.org/docs/install.html#install-from-source) and the [FAQ](https://www.psycopg.org/docs/faq.html#faq-compile) in the Psycopg documentation.

If prerequisites are met, you can install psycopg like any other Python package, using `pip` to download it from [PyPI](https://pypi.org/project/psycopg2/):

```sh
$ pip install psycopg2
```

or, using `setup.py`, if you have downloaded the source package locally:

```sh
python setup.py build
sudo python setup.py install
```

You can also obtain a stand-alone package, not requiring a compiler or external libraries, by installing the [psycopg2-binary](https://pypi.org/project/psycopg2-binary/) package from PyPI:

```sh
pip install psycopg2-binary
```

The binary package is a practical choice for development and testing, but in production it is recommended to use the package built from sources.

### Step 2: Connect to your cluster

Python applications can connect to and query the YugabyteDB database. First, import the psycopg2 package.

```python
import psycopg2
```

You can provide the connection details in one of the following ways:

- Connection string

  ```python
  "dbname=database_name host=hostname port=port user=username password=password"
  ```

- Connection dictionary

  ```python
  user = 'username', password='xxx', host = 'hostname', port = 'port', dbname = 'database_name'
  ```

The following is an example URL for connecting to YugabyteDB.

```python
conn = psycopg2.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
```

| Parameter | Description | Default |
| :---------- | :---------- | :------ |
| host  | Hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database/dbname | Database name | yugabyte
| user | User connecting to the database | yugabyte
| password | User password | yugabyte

The following is an example URL for connecting to YugabyteDB with SSL encryption enabled.

```python
conn = psycopg2.connect("host=<hostname> port=5433 dbname=yugabyte user=<username> password=<password> sslmode=verify-full sslrootcert=/Users/my-user/Downloads/root.crt")
```

#### Use SSL

| Parameter | Description | Default |
| :---------- | :---------- | :------ |
| sslmode | SSL mode  | prefer
| sslrootcert | Path to the root certificate on your computer | ~/.postgresql/

If you have created a cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/), [follow the steps](/preview/yugabyte-cloud/cloud-connect/connect-applications/) to obtain the cluster connection parameters and SSL Root certificate.

### Step 3: Query the YugabyteDB cluster from your application

1. Create a new Python file called `yb-ysql-helloworld.py` in the base package directory of your project.

1. Copy the following sample code to set up tables and query the table contents. Replace the connection string `connString` with the cluster credentials and SSL certificate, if required.

   ```python
   import psycopg2

   # Create the database connection.

   connString = "host=127.0.0.1 port=5433 dbname=yugabyte user=yugabyte password=yugabyte"

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

   When you run the `yb-ysql-helloworld.py` project, you should see output similar to the following:

   ```text
   Created table employee
   Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
   Query returned: John, 35, Python
   ```

   If there is no output or you get an error, verify the parameters included in the connection string.

## Next steps

- Learn how to build Python applications using [Django](../../../drivers-orms/python/django/).
- Learn how to build Python applications using [SQLAlchemy](../../../drivers-orms/python/sqlalchemy/).
- Learn more about [fundamentals](../../../reference/drivers/python/postgres-psycopg2-reference/#fundamentals) of the PostgreSQL psycopg2 driver.
