---
title: PostgreSQL Psycopg3 driver
headerTitle: Python drivers
linkTitle: Python drivers
description: PostgreSQL Psycopg3 Python Driver for YSQL
tags:
  other: ysql
menu:
  stable_develop:
    name: Python drivers
    identifier: ref-postgres-psycopg3-driver
    parent: python-drivers
    weight: 110
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
    <li >
    <a href="../yugabyte-psycopg2-reference/" class="nav-link ">
      <img src="/icons/yugabyte.svg"></i>
      Yugabyte Psycopg2
    </a>
  </li>
  <li >
    <a href="../postgres-psycopg2-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG Psycopg2
    </a>
  </li>
  <li >
    <a href="../postgres-psycopg3-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG Psycopg3
    </a>
  </li>
</ul>

Psycopg 3 is the newest implementation of the most popular adapter for Python.

For details on using psycopg3, see [Psycopg3 documentation](https://www.psycopg.org/psycopg3/docs/).

For a tutorial on building a sample Python application that uses psycopg3, see [Connect an application](../postgres-psycopg3/).

## Fundamentals

Learn how to perform common tasks required for Python application development using the PostgreSQL Psycopg3 driver.

### Download the driver dependency

The quickest way to start developing with Psycopg 3 is to install the binary packages by running:

```sh
pip install "psycopg[binary]"
```

This will install a self-contained package with all the libraries needed. You will need `pip 20.3` at least.

If your platform is not supported, or if the `libpq` packaged is not suitable, you should proceed to a local installation or a pure Python installation.

In order to perform a local installation you need some prerequisites:

- a C compiler,

- Python development headers (e.g. the `python3-dev` package).

- PostgreSQL client development headers (e.g. the `libpq-dev` package).

- The `pg_config` program available in the `PATH`.

If your build prerequisites are in place you can run:

```sh
pip install "psycopg[c]"
```

If you simply install:

```sh
pip install psycopg
```

Without [c] or [binary] extras you will obtain a pure Python implementation. This is particularly handy to debug and hack, but it still requires the system libpq to operate (which will be imported dynamically via `ctypes`).


### Connect to YugabyteDB database

Python applications can connect to and query the YugabyteDB database using the following:

- Import the psycopg package.

    ```python
    import psycopg
    ```

- The Connection details can be provided as a string or a dictionary.

    Connection String

    ```python
    "dbname=database_name host=hostname port=port user=username  password=password"
    ```

    Connection Dictionary

    ```python
    user = 'username', password='xxx', host = 'hostname', port = 'port', dbname = 'database_name'
    ```

The following table describes the connection parameters required to connect to the YugabyteDB database

| Parameters | Description | Default |
| :-------------- | :------------------------- | :---------- |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database/dbname | Database name | yugabyte
| user | User for connecting to the database | yugabyte
| password | Password for the user | yugabyte

The following is an example connection string for connecting to YugabyteDB.

```python
conn = psycopg.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
```

### Executing queries
Psycopg 3 exposes a few simple extensions which make the following pattern leaner:

the Connection objects exposes an execute() method, equivalent to creating a cursor, calling its execute() method, and returning it.

In Psycopg 2:

```python
cur = conn.cursor()
cur.execute(...)
```

In Psycopg 3:

```python
cur = conn.execute(...)
```


### Create tables

Tables can be created in YugabyteDB by passing the `CREATE TABLE` DDL statement to the `conn.execute(statement)` method, using the following example:

```sql
CREATE TABLE IF NOT EXISTS employee (id int PRIMARY KEY, name varchar, age int, language text)
```

```python
conn = psycopg.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
conn.execute('CREATE TABLE IF NOT EXISTS employee (id int PRIMARY KEY, name varchar, age int, language varchar)')
```

### Read and write data

#### Insert data

To write data into YugabyteDB, execute the `INSERT` statement using the `conn.execute(statement)` method.

For example:

```sql
INSERT INTO employee VALUES (1, 'John', 35, 'Java')
```

```python
conn = psycopg.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
conn.execute('INSERT INTO employee VALUES (1, 'John', 35, 'Java')')
```

#### Query data

In psycopg 3, the Cursor.execute() method returns self. This means that you can chain a fetch operation, such as fetchone(), to the execute() call:

```python
# In Psycopg 2:
cur.execute(...)
record = cur.fetchone()

cur.execute(...)
for record in cur:
    ...

# In Psycopg 3:
record = cur.execute(...).fetchone()

for record in cur.execute(...):
    ...
```

Using them together, in simple cases, you can go from creating a connection to using a result in a single expression:

```python
print(psycopg.connect(DSN).execute("SELECT now()").fetchone()[0])
# 2025-06-27 18:15:10.706497+01:00
```