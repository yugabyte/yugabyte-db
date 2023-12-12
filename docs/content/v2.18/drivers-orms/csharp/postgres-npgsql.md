---
title: PostgreSQL Npgsql driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a C# application using PostgreSQL Npgsql driver
menu:
  v2.18:
    identifier: postgres-npgsql-driver
    parent: csharp-drivers
    weight: 420
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../ysql/" class="nav-link">
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
    <a href="../ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB Npgsql Smart Driver
    </a>
  </li>

  <li >
    <a href="../postgres-npgsql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Npgsql Driver
    </a>
  </li>

</ul>

[Npgsql](https://www.npgsql.org) is an open source ADO.NET Data Provider for PostgreSQL. It allows programs written in C#, Visual Basic, and F# to access YugabyteDB.

## CRUD operations

The following sections demonstrate how to perform common tasks required for C# application development.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Add the Npgsql driver dependency

If you are using Visual Studio, add the Npgsql package to your project as follows:

1. Right-click **Dependencies** and choose **Manage Nuget Packages**.
1. Search for `Npgsql` and click **Add Package**.

To add the Npgsql package to your project when not using an IDE, use the following `dotnet` command:

```csharp
dotnet add package Npgsql
```

or any of the other methods mentioned on the [nuget page](https://www.nuget.org/packages/Npgsql/) for Npgsql.

### Step 2: Set up the database connection

After setting up the dependencies, implement a C# client application that uses the Npgsql driver to connect to your YugabyteDB cluster and run a query on the sample data.

Import Npgsql and use the `NpgsqlConnection` class for getting connection objects for the YugabyteDB database that can be used for performing DDLs and DMLs against the database.

The following table describes the connection parameters required to connect to the YugabyteDB database.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| Host      | Host name of the YugabyteDB instance | localhost
| Port      |  Listen port for YSQL | 5433
| Database  | Database name | yugabyte
| Username  | User connecting to the database | yugabyte
| Password  | Password for the user | yugabyte

The following is a basic example connection string for connecting to YugabyteDB.

```csharp
var connStringBuilder = "Host=localhost;Port=5433;Database=yugabyte;Username=yugabyte;Password=password"
NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder)
```

#### Use SSL

Set up the driver properties to configure the credentials and SSL certificates for connecting to your cluster. The following table describes the additional parameters the .NET Npgsql driver requires as part of the connection string when using SSL.

| Npgsql Parameter | Description |
| :---------- | :---------- |
| SslMode  | SSL Mode |
| RootCertificate | Path to the root certificate on your computer |

The following is an example connection string for connecting to YugabyteDB using SSL.

```csharp
var connStringBuilder = new NpgsqlConnectionStringBuilder();
    connStringBuilder.Host = "22420e3a-768b-43da-8dcb-xxxxxx.aws.ybdb.io";
    connStringBuilder.Port = 5433;
    connStringBuilder.SslMode = SslMode.VerifyFull;
    connStringBuilder.RootCertificate = "/root.crt"; //Provide full path to your root CA.
    connStringBuilder.Username = "admin";
    connStringBuilder.Password = "xxxxxx";
    connStringBuilder.Database = "yugabyte";
    CRUD(connStringBuilder.ConnectionString);
```

[YugabyteDB Managed](https://www.yugabyte.com/managed/) clusters require SSL. Refer to [Connect applications](../../../yugabyte-cloud/cloud-connect/connect-applications/) for instructions on how to obtain the cluster connection parameters and download the CA certificate.

Refer to [Configure SSL/TLS](../../../reference/drivers/csharp/postgres-npgsql-reference/#configure-ssl-tls) for more information on Npgsql default and supported SSL modes, and examples for setting up your connection strings when using SSL.

### Step 3: Write your application

Copy the following code to the `Program.cs` file to set up YugbyteDB tables and query the table contents from the C# client. Replace the connection string `connStringBuilder` with the credentials of your cluster, and SSL certificates if required.

{{< warning title="Warning" >}}

On every new connection, the Npgsql driver also makes [extra system table queries to map types](https://github.com/npgsql/npgsql/issues/1486), which adds significant overhead. To turn off this behavior, set the following option in your connection string builder:

```csharp
connStringBuilder.ServerCompatibilityMode = ServerCompatibilityMode.NoTypeLoading;
```

{{< /warning >}}

```csharp
using System;
using Npgsql;

namespace Yugabyte_CSharp_Demo
{
    class Program
    {
        static void Main(string[] args)
        {
            var connStringBuilder = "host=localhost;port=5433;database=yugabyte;userid=yugabyte;password="
            NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder);

            try
            {
                conn.Open();

                NpgsqlCommand empCreateCmd = new NpgsqlCommand("CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar);", conn);
                empCreateCmd.ExecuteNonQuery();
                Console.WriteLine("Created table Employee");

                NpgsqlCommand empInsertCmd = new NpgsqlCommand("INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'CSharp');", conn);
                int numRows = empInsertCmd.ExecuteNonQuery();
                Console.WriteLine("Inserted data (1, 'John', 35, 'CSharp')");

                NpgsqlCommand empPrepCmd = new NpgsqlCommand("SELECT name, age, language FROM employee WHERE id = @EmployeeId", conn);
                empPrepCmd.Parameters.Add("@EmployeeId", NpgsqlTypes.NpgsqlDbType.Integer);

                empPrepCmd.Parameters["@EmployeeId"].Value = 1;
                NpgsqlDataReader reader = empPrepCmd.ExecuteReader();

                Console.WriteLine("Query returned:\nName\tAge\tLanguage");
                while (reader.Read())
                {
                    Console.WriteLine("{0}\t{1}\t{2}", reader.GetString(0), reader.GetInt32(1), reader.GetString(2));
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine("Failure: " + ex.Message);
            }
            finally
            {
                if (conn.State != System.Data.ConnectionState.Closed)
                {
                    conn.Close();
                }
            }
        }
    }
}
```

You should see output similar to the following:

```output
Created table Employee
Inserted data (1, 'John', 35, 'CSharp')
Query returned:
Name  Age  Language
John  35   CSharp
```

### Step 4: Write your application with SSL (optional)

Copy the following code to your `Program.cs` file , and replace the values in the `connStringBuilder` object as appropriate for your cluster if you're using SSL.

```csharp
using System;
using Npgsql;

namespace Yugabyte_CSharp_Demo
{
   class Program
   {
       static void Main(string[] args)
       {
          var connStringBuilder = new NpgsqlConnectionStringBuilder();
           connStringBuilder.Host = "22420e3a-768b-43da-8dcb-xxxxxx.aws.ybdb.io";
           connStringBuilder.Port = 5433;
           connStringBuilder.SslMode = SslMode.VerifyFull;
           connStringBuilder.RootCertificate = "/root.crt" //Provide full path to your root CA.
           connStringBuilder.Username = "admin";
           connStringBuilder.Password = "xxxxxx";
           connStringBuilder.Database = "yugabyte";
           CRUD(connStringBuilder.ConnectionString);
       }
       static void CRUD(string connString)
       {
            NpgsqlConnection conn = new NpgsqlConnection(connString);
           try
           {
               conn.Open();

               NpgsqlCommand empDropCmd = new NpgsqlCommand("DROP TABLE if exists employee;", conn);
               empDropCmd.ExecuteNonQuery();
               Console.WriteLine("Dropped table Employee");

               NpgsqlCommand empCreateCmd = new NpgsqlCommand("CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar);", conn);
               empCreateCmd.ExecuteNonQuery();
               Console.WriteLine("Created table Employee");

               NpgsqlCommand empInsertCmd = new NpgsqlCommand("INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'CSharp');", conn);
               int numRows = empInsertCmd.ExecuteNonQuery();
               Console.WriteLine("Inserted data (1, 'John', 35, 'CSharp + SSL')");

               NpgsqlCommand empPrepCmd = new NpgsqlCommand("SELECT name, age, language FROM employee WHERE id = @EmployeeId", conn);
               empPrepCmd.Parameters.Add("@EmployeeId", NpgsqlTypes.NpgsqlDbType.Integer);

               empPrepCmd.Parameters["@EmployeeId"].Value = 1;
               NpgsqlDataReader reader = empPrepCmd.ExecuteReader();

               Console.WriteLine("Query returned:\nName\tAge\tLanguage");
               while (reader.Read())
               {
                   Console.WriteLine("{0}\t{1}\t{2}", reader.GetString(0), reader.GetInt32(1), reader.GetString(2));
               }
           }
           catch (Exception ex)
           {
               Console.WriteLine("Failure: " + ex.Message);
           }
           finally
           {
               if (conn.State != System.Data.ConnectionState.Closed)
               {
                   conn.Close();
               }
           }
       }
   }
}
```

## Run the application

To run the project `Program.cs` in Visual Studio Code, from the **Run** menu, choose **Start Without Debugging**. If you aren't using an IDE, enter the following command:

```csharp
dotnet run
```

You should see output similar to the following if you're using SSL:

```output
Created table Employee
Inserted data (1, 'John', 35, 'CSharp + SSL')
Query returned:
Name  Age  Language
John  35   CSharp + SSL
```

If you receive no output or an error, check the parameters in the connection string.

## Learn more

- [PostgreSQL Npgsql driver reference](../../../reference/drivers/csharp/postgres-npgsql-reference/)
- Build C# applications using [EntityFramework ORM](../entityframework)
