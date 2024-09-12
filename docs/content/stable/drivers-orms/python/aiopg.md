---
title: aiopg Driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Python application using aiopg Driver for YSQL
menu:
  stable:
    identifier: ysql-aiopg
    parent: python-drivers
    weight: 520
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../yugabyte-psycopg2/" class="nav-link">
      YSQL
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../yugabyte-psycopg2" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB Psycopg2 Smart Driver
    </a>
  </li>

  <li >
    <a href="../postgres-psycopg2" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Psycopg2 Driver
    </a>
  </li>

  <li >
    <a href="../aiopg" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      aiopg
    </a>
  </li>

</ul>

The following tutorial creates a basic Python application that connects to a YugabyteDB cluster using the [`aiopg`](https://aiopg.readthedocs.io/en/stable/) database adapter, performs a few basic database operations — creating a table, inserting data, and running a SQL query — and prints the results to the screen.

## Prerequisites

Before you start using Aiopg, ensure you have the following:

- YugabyteDB up and running. If you are new to YugabyteDB, follow the steps in [Quick start](/preview/quick-start/linux/) to have YugabyteDB up and running in minutes.
- [Python 3](https://www.python.org/downloads/), or later, is installed.
- [aiopg](https://aiopg.readthedocs.io/en/stable/) package is installed. Install the package using the following command:

    ```sh
    pip3 install aiopg
    ```

    For details about using this database adapter, see [aiopg documentation](https://aiopg.readthedocs.io/en/stable/).

## Create the sample Python application

Create a file `yb-sql-helloworld.py` and copy the following code:

```python
import asyncio
import aiopg

dsn = 'dbname=yugabyte user=yugabyte password=yugabyte host=127.0.0.1 port=5433'


async def go():
    async with aiopg.create_pool(dsn) as pool:
        async with pool.acquire() as conn:
            # Open a cursor to perform database operations.
            async with conn.cursor() as cur:
                await cur.execute(f"""
                  DROP TABLE IF EXISTS employee;
                  CREATE TABLE employee (id int PRIMARY KEY,
                             name varchar,
                             age int,
                             language varchar);
                """)
                print("Created table employee")
                # Insert a row.
                await cur.execute("INSERT INTO employee (id, name, age, language) VALUES (%s, %s, %s, %s)",
                                  (1, 'John', 35, 'Python'))
                print("Inserted (id, name, age, language) = (1, 'John', 35, 'Python')")

                # Query the row.
                await cur.execute("SELECT name, age, language FROM employee WHERE id = 1")
                async for row in cur:
                    print("Query returned: %s, %s, %s" % (row[0], row[1], row[2]))


loop = asyncio.get_event_loop()
loop.run_until_complete(go())
```

### Run the application

To use the application, run the following Python script:

```sh
python yb-sql-helloworld.py
```

You should see the following output:

```output
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
Query returned: John, 35, Python
```
