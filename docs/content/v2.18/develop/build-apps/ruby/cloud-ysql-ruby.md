---
title: Build a Ruby application that uses YSQL
headerTitle: Build a Ruby application
description: Build a small Ruby application using the Ruby Pg driver and using the YSQL API to connect to and interact with a YugabyteDB Aeon cluster.
headContent: "Client driver: Ruby Pg"
menu:
  v2.18:
    parent: build-apps
    name: Ruby
    identifier: cloud-ruby
    weight: 700
type: docs
---

The following tutorial shows a small [Ruby application](https://github.com/yugabyte/yugabyte-simple-ruby-app) that connects to a YugabyteDB cluster using the [Ruby Pg driver](../../../../reference/drivers/ysql-client-drivers/#pg) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Aeon in Ruby.

## Prerequisites

- Ruby 3.1 or later.
- OpenSSL 1.1.1 or later (used by libpq and pg to establish secure SSL connections).
- [libpq](../../../../reference/drivers/ysql-client-drivers/#libpq). Homebrew users on macOS can install using `brew install libpq`. You can download the PostgreSQL binaries and source from [PostgreSQL Downloads](https://www.postgresql.org/download/).
- [Ruby pg](../../../../reference/drivers/ysql-client-drivers/#pg). To install Ruby pg, run the following command:

    ```sh
    gem install pg -- --with-pg-include=<path-to-libpq>/libpq/include --with-pg-lib=<path-to-libpq>/libpq/lib
    ```

    Replace `<path-to-libpq>` with the path to the libpq installation; for example, `/usr/local/opt`.

### Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-ruby-app.git && cd yugabyte-simple-ruby-app
```

## Provide connection parameters

If your cluster is running on YugabyteDB Aeon, you need to modify the connection parameters so that the application can establish a connection to the YugabyteDB cluster. (You can skip this step if your cluster is running locally and listening on 127.0.0.1:5433.)

To do this:

1. Open the `sample-app.rb` file.

2. Set the following configuration-related parameters:

    - **host** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Aeon, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
    - **dbname** - the name of the database you are connecting to (the default is `yugabyte`).
    - **user** and **password** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte` and `yugabyte`). For YugabyteDB Aeon, use the credentials in the credentials file you downloaded.
    - **sslmode** - the SSL mode to use. YugabyteDB Aeon [requires SSL connections](../../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/); use `verify-full`.
    - **sslrootcert** - the full path to the YugabyteDB Aeon cluster CA certificate.

3. Save the file.

## Build and run the application

Make the application file executable.

```sh
chmod +x sample-app.rb
```

Run the application.

```sh
$ ./sample-app.rb
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

You have successfully executed a basic Ruby application that works with YugabyteDB Aeon.

## Explore the application logic

Open the `sample-app.rb` file in the `yugabyte-simple-ruby-app` folder to review the methods.

### connect

The `connect` method establishes a connection with your cluster via the libpqxx driver.

```ruby
conn = PG.connect(
    host: '',
    port: '5433',
    dbname: 'yugabyte',
    user: '',
    password: '',
    sslmode: 'verify-full',
    sslrootcert: ''
    );
```

### create_database

The `create_database` method uses PostgreSQL-compliant DDL commands to create a sample database.

```ruby
conn.exec("DROP TABLE IF EXISTS DemoAccount");

conn.exec("CREATE TABLE DemoAccount ( \
            id int PRIMARY KEY, \
            name varchar, \
            age int, \
            country varchar, \
            balance int)");

conn.exec("INSERT INTO DemoAccount VALUES \
            (1, 'Jessica', 28, 'USA', 10000), \
            (2, 'John', 28, 'Canada', 9000)");
```

### select_accounts

The `select_accounts` method queries your distributed data using the SQL `SELECT` statement.

```ruby
begin
    puts ">>>> Selecting accounts:\n";

    rs = conn.exec("SELECT name, age, country, balance FROM DemoAccount");

    rs.each do |row|
        puts "name=%s, age=%s, country=%s, balance=%s\n" % [row['name'], row['age'], row['country'], row['balance']];
    end

ensure
    rs.clear if rs
end
```

### transfer_money_between_accounts

The `transferMoneyBetweenAccounts` method updates your data consistently with distributed transactions.

```ruby
begin
    conn.transaction do |txn|
        txn.exec_params("UPDATE DemoAccount SET balance = balance - $1 WHERE name = \'Jessica\'", [amount]);
        txn.exec_params("UPDATE DemoAccount SET balance = balance + $1 WHERE name = \'John\'", [amount]);
    end

    puts ">>>> Transferred %s between accounts.\n" % [amount];

rescue PG::TRSerializationFailure => e
    puts "The operation is aborted due to a concurrent transaction that is modifying the same set of rows. \
            Consider adding retry logic for production-grade applications.";
    raise
end
```

## Learn more

[Ruby pg driver](../../../../reference/drivers/ysql-client-drivers/#pg)

[libpq driver](../../../../reference/drivers/ysql-client-drivers/#libpq)
