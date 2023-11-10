---
title: Build a Python application that uses YSQL
headerTitle: Build a Python application
linkTitle: Python
description: Build a simple Python application using the Python psycopg2 driver and using the YSQL API to connect to and interact with a YugabyteDB Managed cluster.
headContent: "Client driver: psycopg2"
menu:
  v2.18:
    parent: build-apps
    name: Python
    identifier: cloud-python
    weight: 300
type: docs
---

The following tutorial shows a small [Python application](https://github.com/yugabyte/yugabyte-simple-python-app) that connects to a YugabyteDB cluster using the [Python psycopg2 PostgreSQL database adapter](../../../../reference/drivers/python/postgres-psycopg2-reference/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Python.

## Prerequisites

Python 3.6 or later (Python 3.9.7 or later if running macOS on Apple silicon).

### Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-python-app.git && cd yugabyte-simple-python-app
```

## Provide connection parameters

If your cluster is running on YugabyteDB Managed, you need to modify the connection parameters so that the application can establish a connection to the YugabyteDB cluster. (You can skip this step if your cluster is running locally and listening on 127.0.0.1:5433.)

To do this:

1. Open the `sample-app.py` file.

2. Set the following configuration parameter constants:

    - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **port** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
    - **dbName** - the name of the database you are connecting to (the default database is named `yugabyte`).
    - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
    - **sslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/); use `verify-full`.
    - **sslRootCert** - the full path to the YugabyteDB Managed cluster CA certificate.

3. Save the file.

## Build and run the application

Install psycopg2 PostgreSQL database adapter.

```sh
$ pip3 install psycopg2-binary
```

Start the application.

```sh
$ python3 sample-app.py
```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Python application that works with YugabyteDB Managed.

## Explore the application logic

Open the `sample-app.py` file in the `yugabyte-simple-python-app` folder to review the methods.

### main

The `main` method establishes a connection with your cluster via the Python PostgreSQL database adapter.

```python
try:
    if conf['sslMode'] != '':
        yb = psycopg2.connect(host=conf['host'], port=conf['port'], database=conf['dbName'],
                                user=conf['dbUser'], password=conf['dbPassword'],
                                sslmode=conf['sslMode'], sslrootcert=conf['sslRootCert'],
                                connect_timeout=10)
    else:
        yb = psycopg2.connect(host=conf['host'], port=conf['port'], database=conf['dbName'],
                                user=conf['dbUser'], password=conf['dbPassword'],
                                connect_timeout=10)
except Exception as e:
    print("Exception while connecting to YugabyteDB")
    print(e)
    exit(1)
```

### create_database

The `create_database` method uses PostgreSQL-compliant DDL commands to create a sample database.

```python
try:
    with yb.cursor() as yb_cursor:
        yb_cursor.execute('DROP TABLE IF EXISTS DemoAccount')

        create_table_stmt = """
            CREATE TABLE DemoAccount (
                id int PRIMARY KEY,
                name varchar,
                age int,
                country varchar,
                balance int
            )"""
        yb_cursor.execute(create_table_stmt)

        insert_stmt = """
            INSERT INTO DemoAccount VALUES
                    (1, 'Jessica', 28, 'USA', 10000),
                    (2, 'John', 28, 'Canada', 9000)"""
        yb_cursor.execute(insert_stmt)
    yb.commit()
except Exception as e:
    print("Exception while creating tables")
    print(e)
    exit(1)
```

### select_accounts

The `select_accounts` method queries your distributed data using the SQL `SELECT` statement.

```python
with yb.cursor(cursor_factory=psycopg2.extras.RealDictCursor) as yb_cursor:
        yb_cursor.execute("SELECT name, age, country, balance FROM DemoAccount")
        results = yb_cursor.fetchall()
        for row in results:
            print("name = {name}, age = {age}, country = {country}, balance = {balance}".format(**row))
```

### transfer_money_between_accounts

The `transfer_money_between_accounts` method updates your data consistently with distributed transactions.

```python
try:
    with yb.cursor() as yb_cursor:
        yb_cursor.execute("UPDATE DemoAccount SET balance = balance - %s WHERE name = 'Jessica'", [amount])
        yb_cursor.execute("UPDATE DemoAccount SET balance = balance + %s WHERE name = 'John'", [amount])
    yb.commit()
except (Exception, psycopg2.DatabaseError) as e:
    print("Exception while transferring money")
    print(e)
    if e.pgcode == 40001:
        print("The operation is aborted due to a concurrent transaction that is modifying the same set of rows." +
                "Consider adding retry logic for production-grade applications.")
    exit(1)
```

## Learn more

[Python psycopg2 PostgreSQL database adapter](../../../../reference/drivers/ysql-client-drivers/#psycopg2)
