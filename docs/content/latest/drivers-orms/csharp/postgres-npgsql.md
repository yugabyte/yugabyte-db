---
title: C# Drivers
linkTitle: C# Drivers
description: C# Drivers for YSQL
headcontent: C# Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    name: C# Drivers
    identifier: postgres-npgsql-driver
    parent: csharp-drivers
    weight: 400
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/drivers-orms/csharp/postgres-npgsql/" class="nav-link active">
      <i class="icon-java-bold" aria-hidden="true"></i>
      Postgres Npgsql Driver
    </a>
  </li>

</ul>

Npgsql is an open source ADO.NET Data Provider for PostgreSQL, it allows programs written in C#, Visual Basic, F# to access YugabyteDB server. It is implemented in 100% C# code, is free and is open source.

## Step 1: Add the Npgsql Driver Dependency

If you are using Visual Studio IDE, follow the below steps:
1. Open your Project Solution View
1. Right-click on **Packages** and click **Add Packages**
1. Search for `Npgsql` and click **Add Package**

To add Npgsql package to your project, when not using an IDE, use the `dotnet` command:
```csharp
dotnet add package Npgsql 
``` 
or any of the other methods mentioned on the [nuget page](https://www.nuget.org/packages/Npgsql/) for Npgsql.

## Step 2: Connect to your Cluster

After setting up the dependenices, we implement the C# client application that uses the Npgsql driver to connect to your YugabyteDB cluster and run query on the sample data.

We will import Npgsql and use the `NpgsqlConnection` class for getting connection object for the YugabyteDB Database which can be used for performing DDLs and DMLs against the database.

Example URL for connecting to YugabyteDB can be seen below.

```csharp
var yburl = "host=localhost;port=5433;database=yb_demo;user id=yugabyte;password="
NpgsqlConnection conn = new NpgsqlConnection(yburl)
```

| Params | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte

The .NET Npgsql driver validates certificates differently from other PostgreSQL drivers. When you specify SSL mode `require`, the driver verifies the certificate by default (like the `verify-ca` or `verify-full` modes), and fails for self-signed certificates(like YugabyteDB's). You can override this by specifying "Trust Server Certificate=true", in which case it bypasses walking the certificate chain to validate trust and hence works like other drivers' `require` mode. In this case, the Root-CA certificate is not required to be configured.

## Step 3: Query the YugabyteDB Cluster from Your Application

Next, copy the sample code below in the Program.cs file in order to setup a YugbyteDB Tables and query the Table contents from the java client. Ensure you replace the connection string `yburl` with credentials of your cluster and SSL certs if required.

```csharp
using System;
using Npgsql;

namespace Yugabyte_CSharp_Demo
{
    class Program
    {
        static void Main(string[] args)
        {
            NpgsqlConnection conn = new NpgsqlConnection("host=localhost;port=5433;database=yb_demo;user id=yugabyte;password=");

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

When you run the Project, it should output something like below:

```output
Created table Employee
Inserted data (1, 'John', 35, 'CSharp')
Query returned:
Name  Age  Language
John  35   CSharp
```

if you receive no output or error, check whether you included the proper connection string in your Program.cs file with the right credentials.

After completing these steps, you should have a working C# app that uses the YugabyteDB JDBC driver to connect to your cluster, set up tables, run a query, and print out results.

## Next Steps
