---
title: Build a PHP application that uses YSQL
headerTitle: Build a PHP application
description: Build a small PHP application using the php-pgsql driver and using the YSQL API to connect to and interact with a Yugabyte Cloud cluster.
menu:
  latest:
    parent: cloud-build-apps
    name: PHP
    identifier: cloud-php
    weight: 900
type: page
isTocNested: true
showAsideToc: true
---

The following tutorial shows a small [PHP application](https://github.com/yugabyte/yugabyte-simple-php-app) that connects to a YugabyteDB cluster using the [php-pgsql](../../../../reference/drivers/ysql-client-drivers/#php-pgsql) driver and performs basic SQL operations. Use the application as a template to get started with Yugabyte Cloud in PHP.

## Prerequisites

- PHP runtime. The sample application was created using PHP 8.1 but should work with earlier and later versions. Homebrew users on macOS can install PHP using `brew install php`; the php-pgsql driver is installed automatically.
- [php-pgsql driver](https://www.php.net/manual/en/pgsql.setup.php).
  - Homebrew users of earlier PHP versions can install the driver using the `brew install phpXX-pdo-pgsql` command, where XX is the PHP version.
  - Ubuntu users can install the driver using the `sudo apt-get install php-pgsql` command.
  - CentOS users can install the driver using the `sudo yum install php-pgsql` command.

### Yugabyte Cloud

- You have a cluster deployed in Yugabyte Cloud. To get started, use the [Quick start](../../).
- You downloaded the cluster CA certificate. Refer to [Download your cluster certificate](../../../cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate).
- You have added your computer to the cluster IP allow list. Refer to [Assign IP Allow Lists](../../../cloud-secure-clusters/add-connections/).

## Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/yugabyte/yugabyte-simple-php-app && cd yugabyte-simple-php-app
```

## Provide connection parameters

The application needs to establish a connection to the YugabyteDB cluster. To do this:

1. Open the `sample-app.php` file.

2. Set the following configuration-related constants:

    - **HOST** - the host name of your YugabyteDB cluster. To obtain a Yugabyte Cloud cluster host name, sign in to Yugabyte Cloud, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Network Access**.
    - **PORT** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
    - **DB_NAME** - the name of the database you are connecting to (the default database is named `yugabyte`).
    - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in Yugabyte Cloud, these can be found in the credentials file you downloaded.
    - **SSL_MODE** - the SSL mode to use. Yugabyte Cloud [requires SSL connections](../../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
    - **SSL_ROOT_CERT** - the full path to the Yugabyte Cloud cluster CA certificate.

3. Save the file.

## Run the application

Run the application.

```sh
$ php sample-app.php
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

You have successfully executed a basic PHP application that works with Yugabyte Cloud.

## Explore the application logic

Open the `sample-app.php` file in the `yugabyte-simple-php-app` folder to review the methods.

### connect

The `connect` method establishes a connection with your cluster via the php-pgsql driver.

```php
$conn = new PDO('pgsql:host=' . HOST . ';port=' . PORT . ';dbname=' . DB_NAME .
                ';sslmode=' . SSL_MODE . ';sslrootcert=' . SSL_ROOT_CERT,
                USER, PASSWORD,
                array(PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                        PDO::ATTR_EMULATE_PREPARES => true,
                        PDO::ATTR_PERSISTENT => true));
```

### create_database

The `create_database` method uses PostgreSQL-compliant DDL commands to create a sample database.

```php
$conn->exec('DROP TABLE IF EXISTS DemoAccount');

$conn->exec('CREATE TABLE DemoAccount ( 
                id int PRIMARY KEY, 
                name varchar, 
                age int, 
                country varchar, 
                balance int)');

$conn->exec("INSERT INTO DemoAccount VALUES 
                (1, 'Jessica', 28, 'USA', 10000), 
                (2, 'John', 28, 'Canada', 9000)");
```

### select_accounts

The `select_accounts` method queries your distributed data using the SQL `SELECT` statement.

```php
$query = 'SELECT name, age, country, balance FROM DemoAccount';

foreach ($conn->query($query) as $row) {
    print 'name=' . $row['name'] . ', age=' . $row['age'] . ', country=' . $row['country'] . ', balance=' . $row['balance'] . "\n";
}
```

### transfer_money_between_accounts

The `transfer_money_between_accounts` method updates your data consistently with distributed transactions.

```php
try {
    $conn->beginTransaction();

    $conn->exec("UPDATE DemoAccount SET balance = balance - " . $amount . " WHERE name = 'Jessica'");
    
    $conn->exec("UPDATE DemoAccount SET balance = balance + " . $amount . " WHERE name = 'John'");

    $conn->commit();

    print ">>>> Transferred " . $amount . " between accounts\n";

} catch (PDOException $e) {
    if ($e->getCode() == '40001') {
        print "The operation is aborted due to a concurrent transaction that is modifying the same set of rows.
                Consider adding retry logic for production-grade applications.\n";
    }
    
    throw $e;
}
```

## Learn more

[php-pgsql driver](../../../../reference/drivers/ysql-client-drivers/#php-pgsql)

[Explore more applications](../../../cloud-develop)

[Deploy clusters in Yugabyte Cloud](../../../cloud-basics)

[Connect to applications in Yugabyte Cloud](../../../cloud-connect/connect-applications/)
