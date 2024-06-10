---
title: Build a C# application that uses YSQL
headerTitle: Build a C# application
linkTitle: C#
description: Use C# to build a YugabyteDB application that uses YSQL
menu:
  v2.14:
    identifier: build-apps-csharp-1-ysql
    parent: build-apps
    weight: 554
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./ysql.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li>
    <a href="../ysql-entity-framework/" class="nav-link ">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Entity Framework
    </a>
  </li>
  <li>
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

{{< tip title="YugabyteDB Managed requires SSL" >}}

Are you using YugabyteDB Managed? Install the [prerequisites](#prerequisites), then go to the [Use C# with SSL](#use-c-with-ssl) section.

{{</ tip >}}

## Prerequisites

This tutorial assumes that you have installed the following:

- YugabyteDB, created a universe, and are able to interact with it using the YSQL shell (`ysqlsh`). If not, follow the steps in [Quick start](../../../../quick-start/).
- Visual Studio.
- [.NET SDK 6.0](https://dotnet.microsoft.com/en-us/download) or later.

{{< warning title="Warning" >}}

On every new connection, the Npgsql driver also makes [extra system table queries to map types](https://github.com/npgsql/npgsql/issues/1486), which adds significant overhead. To turn off this behavior, set the following option in your connection string builder:

```csharp
connStringBuilder.ServerCompatibilityMode = ServerCompatibilityMode.NoTypeLoading;
```

{{< /warning >}}

## Create a sample C# application

To create the sample C# application, do the following:

1. In Visual Studio, create a new C# application, and choose [Console Application](https://docs.microsoft.com/en-us/dotnet/core/tutorials/with-visual-studio?pivots=dotnet-6-0) as template. Follow the instructions to save the project.

1. Add the Npgsql package to your project as follows:

   - Right-click on **Dependencies** and click **Manage Nuget Packages**.
   - Search for `Npgsql` and click **Add Package**.

1. Copy the following code to your `Program.cs` file:

```csharp
using System;
using Npgsql;

namespace Yugabyte_CSharp_Demo
{
    class Program
    {
        static void Main(string[] args)
        {
           var connStringBuilder = "host=localhost;port=5433;database=yugabyte;user id=yugabyte;password="
           NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder)

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

### Run the C# application

To run the application, choose **Run -> Start Without Debugging**.

You should see the following output:

```output
Created table Employee
Inserted data (1, 'John', 35, 'CSharp')
Query returned:
Name  Age  Language
John  35   CSharp
```

## Use C# with SSL

Refer to [Configure SSL/TLS](../../../../reference/drivers/csharp/postgres-npgsql-reference/#configure-ssl-tls) for information on supported SSL modes and examples for setting up your connection strings.

### Create a sample C# application with SSL

To create the sample C# application, do the following:

1. In Visual Studio, create a new C# application, and choose [Console Application](https://docs.microsoft.com/en-us/dotnet/core/tutorials/with-visual-studio?pivots=dotnet-6-0) as the template. Follow the instructions to save the project.

1. Add the Npgsql package to your project as follows:

   - Right-click on **Dependencies** and click **Manage Nuget Packages**.
   - Search for `Npgsql` and click **Add Package**.

1. Copy the following code to your `Program.cs` file, and replace the values in the `connStringBuilder` object as appropriate for your cluster.

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

### Run the C# SSL application

To run the application, choose **Run -> Start Without Debugging**.

You should see the following output:

```output
Created table Employee
Inserted data (1, 'John', 35, 'CSharp + SSL')
Query returned:
Name  Age  Language
John  35   CSharp + SSL
```
