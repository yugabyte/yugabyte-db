---
title: PostgreSQL Psycopg3 Driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Python application using PostgreSQL Psycopg3 Driver for YSQL
menu:
  stable_develop:
    identifier: postgres-psycopg3-driver
    parent: python-drivers
    weight: 510
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
      <img src="/icons/yugabyte.svg"></i>
      Yugabyte Psycopg2
    </a>
  </li>

  <li >
    <a href="../postgres-psycopg2" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG Psycopg2
    </a>
  </li>

  <li >
    <a href="../postgres-psycopg3" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG Psycopg3
    </a>
  </li>

  <li >
    <a href="../aiopg" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      aiopg
    </a>
  </li>

</ul>

Psycopg3 design emerges from the experience of more than 10 years of development and support of psycopg2. It embraces the new possibilities offered by the more modern generations of the Python language and the PostgreSQL database and addresses the challenges offered by the current patterns in software development and deployment.

## CRUD operations

The following sections demonstrate how to perform common tasks required for Python application development using the PostgreSQL Psycopg3 driver.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Download the driver dependency

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


### Step 2: Connect to your cluster

The following table describes the connection parameters required to connect.

| Parameter | Description | Default |
| :---------- | :---------- | :------ |
| host  | Hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database/dbname | Database name | yugabyte
| user | User connecting to the database | yugabyte
| password | User password | yugabyte

You can provide the connection details in one of the following ways:

- Connection string

  ```python
  "dbname=database_name host=hostname port=port user=username password=password"
  ```

- Connection dictionary

  ```python
  user = 'username', password='xxx', host = 'hostname', port = 'port', dbname = 'database_name'
  ```

The following is an example connection string for connecting to YugabyteDB.

```python
conn = psycopg.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
```

### Step 3: Write your application

Create a new Python file called `QuickStartApp.py` in the base package directory of your project.

Copy the following sample code to set up tables and query the table contents. Replace the connection string `connString` with the cluster credentials and SSL certificate, if required.

```python
# Note: the module name is psycopg, not psycopg3
import psycopg

# Connect to an existing database
with psycopg.connect("host=127.0.0.1 dbname=yugabyte user=yugabyte port=5433") as conn:

    # Open a cursor to perform database operations
    with conn.cursor() as cur:

        # Drop the table if it already exists
        cur.execute("DROP TABLE IF EXISTS employee")

        # Execute a command: this creates a new table
        cur.execute("""
            CREATE TABLE employee (
                id serial PRIMARY KEY,
                name text NOT NULL,
                department text,
                salary numeric
            )
        """)

        # Pass data to fill a query placeholders and let Psycopg perform
        # the correct conversion (no SQL injections!)
        cur.execute(
            "INSERT INTO employee (name, department, salary) VALUES (%s, %s, %s)",
            ("Alice Johnson", "Engineering", 95000.00)
        )

        # Fetch and print one record
        cur.execute("SELECT * FROM employee")
        print("Single record:", cur.fetchone())

        # Batch insert multiple employee records
        employees = [
            ("Bob Smith", "Marketing", 70000.00),
            ("Carol Lee", "HR", 65000.00),
            ("David Kim", "Engineering", 98000.00)
        ]
        cur.executemany(
            "INSERT INTO employee (name, department, salary) VALUES (%s, %s, %s)",
            employees
        )

        # Fetch and print all employee records ordered by salary
        cur.execute("SELECT id, name, salary FROM employee ORDER BY salary")
        print("All employees ordered by salary:")
        for record in cur:
            print(record)

        # Commit the transaction
        conn.commit()

```

Run the project `QuickStartApp.py` using the following command:

```python
python3 QuickStartApp.py
```

You should see output similar to the following:

```text
<psycopg.Cursor [COMMAND_OK] [INTRANS] (host=127.0.0.1 port=5433 database=yugabyte) at 0x1051c34d0>
<psycopg.Cursor [COMMAND_OK] [INTRANS] (host=127.0.0.1 port=5433 database=yugabyte) at 0x1051c34d0>
<psycopg.Cursor [COMMAND_OK] [INTRANS] (host=127.0.0.1 port=5433 database=yugabyte) at 0x1051c34d0>
<psycopg.Cursor [TUPLES_OK] [INTRANS] (host=127.0.0.1 port=5433 database=yugabyte) at 0x1051c34d0>
Single record: (1, 'Alice Johnson', 'Engineering', Decimal('95000'))
<psycopg.Cursor [TUPLES_OK] [INTRANS] (host=127.0.0.1 port=5433 database=yugabyte) at 0x1051c34d0>
All employees ordered by salary:
(3, 'Carol Lee', Decimal('65000'))
(2, 'Bob Smith', Decimal('70000'))
(1, 'Alice Johnson', Decimal('95000'))
(4, 'David Kim', Decimal('98000'))
>>>
```

If there is no output or you get an error, verify the parameters included in the connection string.

## Learn more

[YugabyteDB smart drivers for YSQL](../../smart-drivers/)
