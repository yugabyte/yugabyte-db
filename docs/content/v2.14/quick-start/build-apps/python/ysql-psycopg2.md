---
title: Build a Python application that uses YSQL and psycopg2
headerTitle: Build a Python application
linkTitle: Python
description: Build a sample Python application with psycopg2 that use YSQL.
menu:
  v2.14:
    parent: build-apps
    name: Python
    identifier: python-1
    weight: 553
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./ysql-psycopg2.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - psycopg2
    </a>
  </li>
  <li >
    <a href="{{< relref "./ysql-aiopg.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - aiopg
    </a>
  </li>
  <li >
    <a href="{{< relref "./ysql-sqlalchemy.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - SQL Alchemy
    </a>
  </li>
  <li>
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li>
    <a href="{{< relref "./ysql-django.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Django
    </a>
  </li>
</ul>

{{< tip title="YugabyteDB Managed requires SSL" >}}

Are you using YugabyteDB Managed? Install the [prerequisites](#prerequisites), then go to the [Use Python with SSL](#use-python-with-ssl) section.

{{</ tip >}}

The following tutorial creates a simple Python application that connects to a YugabyteDB cluster using the `psycopg2` database adapter, performs a few basic database operations — creating a table, inserting data, and running a SQL query — and prints the results to the screen.

## Prerequisites

This tutorial assumes that you have satisfied the following prerequisites.

* YugabyteDB is up and running. If you are new to YugabyteDB, you can have YugabyteDB up and running within five minutes by following the steps in [Quick start](../../../../quick-start/).
* Python 3, or later, is installed.
* [Psycopg 2](http://initd.org/psycopg/), the popular PostgreSQL database adapter for Python, is installed.

    \
    To install a binary version of `psycopg2`, run the following `pip3` command:

    ```sh
    $ pip3 install psycopg2-binary
    ```

    \
    For details about using this database adapter, see [Psycopg documentation](http://initd.org/psycopg/docs/).

## Create a sample Python application

Create a file `yb-ysql-helloworld.py` and add the following content:

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

### Run the application

To run the application, type the following:

```sh
$ python3 yb-ysql-helloworld.py
```

You should see the following output:

```output
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
Query returned: John, 35, Python
```

## Use Python with SSL

The client driver supports several SSL modes, as follows:

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Supported |
| prefer (default) | Supported |
| require | Supported |
| verify-ca | Supported |
| verify-full | Supported |

By default, the driver supports the `prefer` SSL mode. And in the `require` mode, a root CA certificate isn't required to be configured.

To enable `verify-ca` or `verify-full`, you need to provide the path to the root CA certificate in the connection string using the `sslrootcert` parameter. The default location is `~/.postgresql/root.crt`. If the root certificate is in a different file, specify it in the `sslrootcert` parameter:

```python
conn = psycopg2.connect("host=<hostname> port=5433 dbname=yugabyte user=<username> password=<password> sslmode=verify-full sslrootcert=/Users/my-user/Downloads/root.crt")
```

The difference between `verify-ca` and `verify-full` depends on the policy of the root CA. If you're using a public CA, verify-ca allows connections to a server that somebody else may have registered with the CA. Because of this behavior, you should always use verify-full with a public CA. If you're using a local CA, or even a self-signed certificate, using verify-ca may provide enough protection, but the best security practice is to always use verify-full.

### Create a sample Python application with SSL

Create a file `yb-ysql-helloworld-ssl.py` and copy the following content to it, replacing the values in the `conn` object as appropriate for your cluster:

```python
import psycopg2

# Create the database connection.

conn = psycopg2.connect("host=<hostname> port=5433 dbname=yugabyte user=<username> password=<password> sslmode=verify-full sslrootcert=<path>")

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
           (1, 'John', 35, 'Python + SSL'))
print("Inserted (id, name, age, language) = (1, 'John', 35, 'Python + SSL')")

# Query the row.

cur.execute("SELECT name, age, language FROM employee WHERE id = 1")
row = cur.fetchone()
print("Query returned: %s, %s, %s" % (row[0], row[1], row[2]))

# Commit and close down.

conn.commit()
cur.close()
conn.close()
```

### Run the application

To run the application, type the following:

```sh
$ python3 yb-ysql-helloworld-ssl.py
```

You should see the following output:

```output
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python + SSL')
Query returned: John, 35, Python + SSL
```
