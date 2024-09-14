---
title: Elixir application that uses Phoenix, Ecto and YSQL
headerTitle: Phoenix Example Application
linkTitle: Phoenix
description: Elixir application that uses Phoenix, Ecto and YSQL
menu:
  preview:
    identifier: elixir-phoenix
    parent: elixir-drivers
    weight: 550
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../phoenix" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      Phoenix/Ecto
    </a>
  </li>
</ul>

The following tutorial shows how to implement an Elixir app using the Phoenix framework with the Ecto ORM. The app connects to a YugabyteDB cluster, creates a sample table, populates it with a few records, and changes data transactionally.

The source for the application can be found in the [yugabytedb-simple-elixir-app](https://github.com/YugabyteDB-Samples/yugabytedb-simple-elixir-app/tree/main/phoenix/simple_app) repository.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../../quick-start/).

- The latest versions of [Elixir, Erlang VM, IEx and Mix](https://elixir-lang.org/docs.html) (tested with Elixir 1.17.1 and Erlang/OTP 26 erts-14.2.5).

## Create Phoenix project

In this section you create a Phoenix project and configure it to work with an existing YugabyteDB cluster.

First, use the `mix` tool to create a project template and pull all required dependencies:

- Generate the project template and navigate to its root folder:

    ```shell
    mix phx.new sample_yugabytedb_phoenix_app && cd sample_yugabytedb_phoenix_app
    ```

- Pull the required dependencies, unless you've already done this during the project generation:

    ```shell
    mix deps.get
    ```

Phoenix uses PostgreSQL as a default database option. Open the `config/dev.exs` file to confirm that the Ecto ORM is configured to work PostgreSQL:

```elixir
# Configure your database
config :sample_yugabytedb_phoenix_app, SampleYugabytedbPhoenixApp.Repo,
  username: "postgres",
  password: "postgres",
  hostname: "localhost",
  database: "sample_yugabytedb_phoenix_app_dev",
  stacktrace: true,
  show_sensitive_data_on_connection_error: true,
  pool_size: 10
```

As YugabyteDB is built on PostgreSQL, you only need to update the `config` settings. Both Ecto and the underlying Postgrex driver will work with YugabyteDB as they do with vanilla PostgreSQL.

Assuming that your YugabyteDB cluster is running locally, do the following:

- Open the `config/dev.exs` file

- Update the database settings to the following:

    ```elixir
    config :sample_yugabytedb_phoenix_app, SampleYugabytedbPhoenixApp.Repo,
    username: "yugabyte",
    password: "yugabyte",
    hostname: "localhost",
    port: 5433,
    database: "yugabyte",
    stacktrace: true,
    show_sensitive_data_on_connection_error: true,
    pool_size: 10
    ```

- Add the `migration_lock: false` setting to the database configuration. Ecto acquires exclusive table locks while running database migrations, but this type of lock is not currently supported by YugabyteDB. Track [GitHub issue 5384](https://github.com/yugabyte/yugabyte-db/issues/5384) to understand details and get updates.

    ```elixir
    config :sample_yugabytedb_phoenix_app, SampleYugabytedbPhoenixApp.Repo,
    <!-- other database specific settings -->
    pool_size: 10,
    migration_lock: false
    ```

- Have Ecto connecting to YugabyteDB to complete and confirm the configuration settings:

    ```shell
    mix ecto.create
    ```

The output should be as follows:

```output
Compiling 15 files (.ex)
Generated sample_yugabytedb_phoenix_app app
The database for SampleYugabytedbPhoenixApp.Repo has already been created
```

## Generate sample schema

In this section, you use Phoenix and Ecto to generate a sample database schema for the `Account` entity and to apply database migrations.

- Generate the schema for the `Account` entity. Note, the `id` field of the `bigint` type will be added to the generated table automatically:

    ```shell
    mix phx.gen.schema Account account name:string balance:decimal
    ```

- Open the `lib/sample_yugabytedb_phoenix_app/account.ex` to see the generated Ecto schema:

    ```elixir
    defmodule SampleYugabytedbPhoenixApp.Account do
    use Ecto.Schema
    import Ecto.Changeset

    schema "account" do
        field :name, :string
        field :balance, :decimal

        timestamps(type: :utc_datetime)
    end

    <!-- other logic -->

    end
    ```

- Run the database migration file that was also generated automatically and placed under the `priv/repo/migrations` directory:

    ```shell
    mix ecto.migrate
    ```

    The output should be as follows:

    ```output
    Compiling 1 file (.ex)

    Generated sample_yugabytedb_phoenix_app app

    10:58:16.251 [info] == Running 20240725145331 SampleYugabytedbPhoenixApp.Repo.Migrations.CreateAccount.change/0 forward

    10:58:16.252 [info] create table account

    10:58:16.443 [info] == Migrated 20240725145331 in 0.1s
    ```

Next, connect to your YugabyteDB cluster using psql, ysqlsh, or another SQL tool to confirm the tables were successfully created:

```sql
yugabyte=# \d
                List of relations
 Schema |       Name        |   Type   |  Owner
--------+-------------------+----------+----------
 public | account           | table    | yugabyte
 public | account_id_seq    | sequence | yugabyte
 public | schema_migrations | table    | yugabyte
(3 rows)
```

## Persist and query data

In this final section, you use Phoenix APIs and the IEx interactive shell to persist and query data in YugabyteDB.

- Start an IEx session in your terminal:

    ```shell
    iex -S mix
    ```

- Create aliases for the `Repo` and `Account` modules:

    ```elixir
    alias SampleYugabytedbPhoenixApp.{Repo, Account}
    ```

- Insert two user accounts into the database:

    ```elixir
    Repo.insert(%Account{name: "John Smith", balance: 1000})
    Repo.insert(%Account{name: "Mary Black", balance: 1500})
    ```

- Fetch the accounts from the database:

    ```elixir
    Repo.all(Account)
    ```

    The output should be as follows:

    ```elixir
    [debug] QUERY OK source="account" db=25.7ms queue=47.5ms idle=1922.1ms

    SELECT a0."id", a0."name", a0."balance", a0."inserted_at", a0."updated_at" FROM "account" AS a0 []
    ↳ :elixir.eval_external_handler/3, at: src/elixir.erl:386
    [
    %SampleYugabytedbPhoenixApp.Account{
        __meta__: #Ecto.Schema.Metadata<:loaded, "account">,
        id: 101,
        name: "Mary Black",
        balance: Decimal.new("1500"),
        inserted_at: ~U[2024-07-25 15:11:12Z],
        updated_at: ~U[2024-07-25 15:11:12Z]
    },
    %SampleYugabytedbPhoenixApp.Account{
        __meta__: #Ecto.Schema.Metadata<:loaded, "account">,
        id: 1,
        name: "John Smith",
        balance: Decimal.new("1000"),
        inserted_at: ~U[2024-07-25 15:11:12Z],
        updated_at: ~U[2024-07-25 15:11:12Z]
    }
    ]
    ```

Note that the `id` of the `Mary Black` account is `101` and not `2`. This happens because sequences in YugabyteDB cache `100` IDs per connection by default. This means that records were inserted into the database using different database connections.

```sql
yugabyte=# \d account_id_seq
```

```output
                       Sequence "public.account_id_seq"
  Type  | Start | Minimum |       Maximum       | Increment | Cycles? | Cache
--------+-------+---------+---------------------+-----------+---------+-------
 bigint |     1 |       1 | 9223372036854775807 |         1 | no      |   100
Owned by: public.account.id
```

For details on how sequences work in YugabyteDB and how to scale them, refer to the [Scaling Sequences with Server-Level Caching in YugabyteDB](https://www.yugabyte.com/blog/create-database-sequences/).

Finally, use the `Ecto.Query` interface to query specific data:

```elixir
import Ecto.Query

Repo.all(from a in Account, where: a.balance >= 1100,
    select: %{a.name => a.balance})
```

The output should be as follows:

```elixir
[%{"Mary Black" => Decimal.new("1500")}]
```

Congratulations! You've successfully completed the tutorial.

Now, explore the [final version of the application](https://github.com/YugabyteDB-Samples/yugabytedb-simple-elixir-app/tree/main/phoenix/simple_app) to see how work with YugabyteDB using the same APIs but programmatically.

## Next steps

- [Watch](https://www.youtube.com/live/_utOXl3eWoA?feature=shared) Chris McCord, the creator of the Phoenix framework, joining YugabyteDB Community Open Hours and demonstrating how to build multi-region applications using Phoenix, Fly.io and YugabyteDB.

- Create a [sample Elixir app](/preview/tutorials/build-apps/elixir/cloud-ysql-elixir/) using the Postgrex driver.
