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

After completing this steps, you should have a working C# app that uses YugabyteDB JDBC driver for connecting to your cluster, setup tables, run query and print out results.

## Working with Domain Objects (ORMs)

In the previous section, you ran a SQL query on a sample table and displayed the results set. In this section, we'll lear to use the C# Objects (Domain Objects) to store and retrive data from YugabyteDB Cluster.

An Object Relational Mapping (ORM) tool is used by the developers to handle database access, it allows developers to map their object-oriented domain classes into the database tables. It simplifies the CRUD operations on your domain objects and easily allow the evolution of Domain objects to be applied to the Database tables.

[EntityFramework](https://docs.microsoft.com/en-us/ef/) is a popular ORM provider for C# applications which is widely used by C# Developers for Database access. YugabyteDB provides full support for EntityFramework ORM.

### Add the Hibernate ORM Dependency

If you are using Visual Studio IDE, follow the below steps:
1. Open your Project Solution View
1. Right-click on **Packages** and click **Add Packages**
1. Search for `Npgsql.EntityFrameworkCore.PostgreSQL` and click **Add Package**

To add Npgsql package to your project, when not using an IDE, use the `dotnet` command:
```csharp
dotnet add package Npgsql.EntityFrameworkCore.PostgreSQL 
``` 
or any of the other methods mentioned on the [nuget page](https://www.nuget.org/packages/Npgsql.EntityFrameworkCore.PostgreSQL) for EntityFramework.

## Step 4: Implementing ORM mapping for YugabyteDB

Create a file called `Model.cs` in the base package directory of your project and add the following code for a class that includes the following fields, setters and getters,

```csharp
using System.Collections.Generic;
using Microsoft.EntityFrameworkCore;

namespace ConsoleApp.PostgreSQL
{
    public class BloggingContext : DbContext
    {
        public DbSet<Blog> Blogs { get; set; }
        public DbSet<Post> Posts { get; set; }

        protected override void OnConfiguring(DbContextOptionsBuilder optionsBuilder)
            => optionsBuilder.UseNpgsql("Host=localhost;Port=5433;Database=yugabyte;Username=yugabyte;Password=yugabyte");
    }

    public class Blog
    {
        public int BlogId { get; set; }
        public string Url { get; set; }
        public List<Post> Posts { get; set; }
    }

    public class Post
    {
        public int PostId { get; set; }
        public string Title { get; set; }
        public string Content { get; set; }

        public int BlogId { get; set; }
        public Blog Blog { get; set; }
    }
}
```

After creating the model, we will use EF migrations to create and setup the database. Run the following commands:
```csharp
dotnet tool install --global dotnet-ef
dotnet add package Microsoft.EntityFrameworkCore.Design
dotnet ef migrations add InitialCreate
dotnet ef database update
```
This installs dotnet ef and the design package which is required to run the command on a project. The migrations command scaffolds a migration to create the initial set of tables for the model. The database update command creates the database and applies the new migration to it.

Next, we will finally connect to the database, insert a row, query it and delete it as well. Copy the following sample code to your `Program.cs` file.

```cs
using System;
using System.Linq;

namespace ConsoleApp.PostgreSQL
{
    internal class Program
    {
        private static void Main()
        {
            using (var db = new BloggingContext())
            {
                // Note: This sample requires the database to be created before running.
                // Console.WriteLine($"Database path: {db.DbPath}.");

                // Create
                Console.WriteLine("Inserting a new blog");
                db.Add(new Blog { Url = "http://blogs.abc.com/adonet" });
                db.SaveChanges();

                // Read
                Console.WriteLine("Querying for a blog");
                var blog = db.Blogs
                    .OrderBy(b => b.BlogId)
                    .First();
                Console.WriteLine("ID :" + blog.BlogId + "\nURL:" + blog.Url);

                // Delete
                Console.WriteLine("Deleting the blog");
                db.Remove(blog);
                db.SaveChanges();
            }
        }
    }
}
```
Run the application and verify the results.

```csharp
dotnet run
```

```output
Inserting a new blog
Querying for a blog
ID :1
URL:http://blogs.abc.com/adonet
Deleting the blog
```

## Next Steps
