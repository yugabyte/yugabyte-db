---
title: Build an Elixir application that uses YSQL
headerTitle: Build an Elixir application
linkTitle: Elixir
description: Build a simple Elixir application using the Postgrex driver and YSQL API to connect to and interact with a YugabyteDB Aeon cluster.
headContent: "Client driver: Postgrex"
menu:
  preview_tutorials:
    parent: build-apps
    name: Elixir
    identifier: cloud-elixir
    weight: 500
type: docs
---

The following tutorial shows a small [Elixir application](https://github.com/YugabyteDB-Samples/yugabytedb-simple-elixir-app/tree/main) that connects to a YugabyteDB cluster using the [Elixir Postgrex driver](https://github.com/elixir-ecto/postgrex) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Aeon in Elixir.

## Prerequisites

The latest versions of [Elixir, Erlang VM, IEx and Mix](https://elixir-lang.org/docs.html) (tested with Elixir 1.17.1 and Erlang/OTP 26 erts-14.2.5).

### Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabytedb-simple-elixir-app.git && cd yugabytedb-simple-elixir-app/postgrex/simple_app
```

## Provide connection parameters

If your cluster is running on YugabyteDB Aeon, you need to modify the connection parameters so that the application can establish a connection to the YugabyteDB cluster. (You can skip this step if your cluster is running locally and listening on 127.0.0.1:5433.)

To do this:

1. Open the `lib/simple_app.ex` file.

1. Set the following configuration parameter constants:

    - **hostname** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Aeon, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
    - **database** - the name of the database you are connecting to (the default is `yugabyte`).
    - **username** and **password** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte` and `yugabyte`). For YugabyteDB Aeon, use the credentials in the credentials file you downloaded.
    - **ssl: \[cacertfile:...\]** - the full path to the YugabyteDB Aeon cluster CA certificate. For local clusters, skip or remove this parameter.

1. Save the file.

## Build and run the application

First, add all required dependencies.

```sh
mix deps.get
```

Compile the application and start an IEx session:

```sh
iex -S mix
```

Start the application.

```sh
SimpleApp.start
```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB! PID: #PID<0.221.0>
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
["Jessica", 28, "USA", 10000]
["John", 28, "Canada", 9000]
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
["Jessica", 28, "USA", 9200]
["John", 28, "Canada", 9800]
:ok
```

You have successfully executed a basic Elixir application that works with YugabyteDB.

## Explore the application logic

Open the `simple_app.ex` file in the `yugabytedb-simple-elixir-app/postgrex/simple_app/lib` folder to review the methods.

### start

The `start` method establishes a connection with your cluster via the Elixir Postgrex driver and executes the rest of the application logic.

```elixir
db_config = @db_config
{:ok, pid} = Postgrex.start_link(db_config)
```

### create_database

The `create_database` method creates a sample table and loads it with a few records.

```elixir
Postgrex.query!(pid, "DROP TABLE IF EXISTS DemoAccount", [])

Postgrex.query!(pid, """
CREATE TABLE DemoAccount (
    id int PRIMARY KEY,
    name varchar,
    age int,
    country varchar,
    balance int)
""",[])

Postgrex.query!(pid, """
INSERT INTO DemoAccount VALUES
    (1, 'Jessica', 28, 'USA', 10000),
    (2, 'John', 28, 'Canada', 9000)
""",[])
```

### select_accounts

The `select_accounts` method queries the data from the database.

```elixir
result = Postgrex.query!(pid,
    "SELECT name, age, country, balance FROM DemoAccount", [])

Enum.each(result.rows, fn row ->
    IO.inspect(row)
end)
```

### transfer_money

The `transfer_money` method updates your data consistently with distributed transactions.

```elixir
Postgrex.transaction(pid, fn(conn) ->
    # Deduct amount from Jessica's account
    Postgrex.query!(conn,
    "UPDATE DemoAccount SET balance = balance - $1 WHERE name = 'Jessica'", [amount])

    # Add amount to John's account
    Postgrex.query!(conn,
    "UPDATE DemoAccount SET balance = balance + $1 WHERE name = 'John'", [amount])
end)
```

## Learn more

[YugabyteDB Community Open Hours](https://www.youtube.com/live/_utOXl3eWoA?feature=shared) - featuring Chris McCord, the creator of the Phoenix framework, demonstrating how to build multi-region applications using Phoenix, Fly.io, and YugabyteDB.
