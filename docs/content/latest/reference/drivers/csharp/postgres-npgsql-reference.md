---
title: C# Drivers
linkTitle: C# Drivers
description: C# Drivers for YSQL
headcontent: C# Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    name: C# Drivers
    identifier: ref-postgres-npgsql-driver
    parent: drivers
    weight: 600
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/reference/drivers/csharp/postgres-npgsql-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Npgsql
    </a>
  </li>

</ul>

Npgsql is an open source ADO.NET Data Provider for PostgreSQL, it allows programs written in C#, Visual Basic, F# to access the YugabyteDB server. It is implemented in 100% C# code, is free and is open source.

## Quick Start

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/latest/quick-start/build-apps/csharp/ysql) in the Quick Start section.

## Download the Driver Dependency

If you are using Visual Studio IDE, follow the below steps:
1. Open your Project Solution View
1. Right-click on **Packages** and click **Add Packages**
1. Search for `Npgsql` and click **Add Package**

To add Npgsql package to your project, when not using an IDE, use the `dotnet` command:
```csharp
dotnet add package Npgsql 
``` 
or any of the other methods mentioned on the [nuget page](https://www.nuget.org/packages/Npgsql/) for Npgsql.

## Fundamentals

Learn how to perform the common tasks required for Java App development using the PostgreSQL JDBC driver

<!-- * [Connect to YugabyteDB Database](postgres-jdbc-fundamentals/#connect-to-yugabytedb-database)
* [Configure SSL/TLS](postgres-jdbc-fundamentals/#configure-ssl-tls)
* [Create Table](/postgres-jdbc-fundamentals/#create-table)
* [Read and Write Queries](/postgres-jdbc-fundamentals/#read-and-write-queries) -->

### Connect to YugabyteDB Database

After setting up the dependenices, we implement the C# client application that uses the Npgsql driver to connect to your YugabyteDB cluster and run query on the sample data.

We will import Npgsql and use the `NpgsqlConnection` class for getting connection object for the YugabyteDB Database which can be used for performing DDLs and DMLs against the database.

Example URL for connecting to YugabyteDB can be seen below.

```csharp
var yburl = "host=localhost;port=5433;database=yb_demo;user id=yugabyte;password="
NpgsqlConnection conn = new NpgsqlConnection(yburl);
```

| Params | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte

### Create Table

Tables can be created in YugabyteDB by passing the `CREATE TABLE` DDL statement to the `NpgsqlCommand` class and getting a command object, then calling the `ExecuteNonQuery()` method using this command object.

For example

```sql
CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar)
```

```csharp
conn.Open();
NpgsqlCommand empCreateCmd = new NpgsqlCommand("CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar);", conn);
empCreateCmd.ExecuteNonQuery();
```

### Read and Write Data

#### Insert Data

In order to write data into YugabyteDB, execute the `INSERT` statement using the `NpgsqlCommand` class and get a command object, then call the `ExecuteNonQuery()` method using this command object.

For example

```sql
INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'CSharp');
```

```csharp
NpgsqlCommand empInsertCmd = new NpgsqlCommand("INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'CSharp');", conn);
int numRows = empInsertCmd.ExecuteNonQuery();
```

#### Query Data

In order to query data from YugabyteDB tables, execute the `SELECT` statement using `NpgsqlCommand` class, get the command object and then call the `ExecuteReader()` function using the object. Loop through the reader to get the list of returned rows.

For example

```sql
SELECT * from employee where id=1;
```

```csharp
NpgsqlCommand empPrepCmd = new NpgsqlCommand("SELECT name, age, language FROM employee WHERE id = @EmployeeId", conn);
empPrepCmd.Parameters.Add("@EmployeeId", NpgsqlTypes.NpgsqlDbType.Integer);

empPrepCmd.Parameters["@EmployeeId"].Value = 1;
NpgsqlDataReader reader = empPrepCmd.ExecuteReader();

Console.WriteLine("Query returned:\nName\tAge\tLanguage"); 
while (reader.Read())
{
    Console.WriteLine("{0}\t{1}\t{2}", reader.GetString(0), reader.GetInt32(1), reader.GetString(2));
}
```

### Configure SSL/TLS

For Npgsql versions < 6.0, The client driver supports several SSL modes, as follows:

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Not Supported |
| prefer | Supported |
| require | Supported <br/> (Self-signed certificates aren't supported.) |
| verify-ca | Not Supported  |
| verify-full | Not Supported |

The Npgsql driver does not support the strings `verify-ca` and `verify-full` when specifying the SSL mode.

The .NET Npgsql driver validates certificates differently from other PostgreSQL drivers. When you specify SSL mode `require`, the driver verifies the certificate by default (like the `verify-ca` or `verify-full` modes), and fails for self-signed certificates. You can override this by specifying "Trust Server Certificate=true", in which case it bypasses walking the certificate chain to validate trust and hence works like other drivers' `require` mode. In this case, the Root-CA certificate is not required to be configured.

For versions 6.0 and older,

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Supported |
| prefer | Supported |
| require | Supported  |
| verify-ca | Supported  |
| verify-full | Supported |

SSL Mode=Require currently requires explicitly setting Trust Server Certificate=true as well. This combination should be used with e.g. self-signed certificates.

The default mode in 6.0+ is Prefer, which allows SSL but does not require it, and does not validate certificates.

## Compatibility Matrix

| Driver Version | YugabyteDB Version | Support |
| :------------- | :----------------- | :------ |
| 6.0.3 | 2.11 (latest) | full
| 6.0.3 |  2.8 (stable) | full
| 6.0.3 | 2.6 | full

## Other Usage Examples

- [SSL Example](latest/quick-start/build-apps/csharp/ysql/#create-a-sample-c-application-with-ssl)

## FAQ