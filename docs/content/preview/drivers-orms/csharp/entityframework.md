---
title: C# ORMs
linkTitle: C# ORMs
description: EntityFramework ORM support for YugabyteDB
headcontent: EntityFramework ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: C# ORMs
    identifier: entityframework-orm
    parent: csharp-drivers
    weight: 600
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/preview/drivers-orms/csharp/entityframework/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      EntityFramework ORM
    </a>
  </li>

</ul>

[EntityFramework](https://docs.microsoft.com/en-us/ef/) is a popular ORM provider for C# applications, and is widely used by C# Developers for database access. YugabyteDB provides full support for the EntityFramework ORM.

## CRUD operations with EntityFramework

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in the [Build an application](../../../quick-start/build-apps/csharp/ysql-entity-framework/) page under the Quick start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for C# application development using EntityFramework.

### Step 1: Add the ORM dependency

If you are using Visual Studio IDE, follow these steps:

1. Open your Project Solution View.
1. Right-click on **Packages** and click **Add Packages**.
1. Search for `Npgsql.EntityFrameworkCore.PostgreSQL` and click **Add Package**.

To add the Npgsql package to your project when not using an IDE, use the following `dotnet` command:

```csharp
dotnet add package Npgsql.EntityFrameworkCore.PostgreSQL 
```

or any of the other methods mentioned on the [nuget page](https://www.nuget.org/packages/Npgsql.EntityFrameworkCore.PostgreSQL) for EntityFramework.

### Step 2: Implement ORM mapping for YugabyteDB

Create a file called `Model.cs` in the base package directory of your project and add the following code for a class that includes the following fields, setters, and getters.

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

After creating the model, use EntityFramework migrations to create and set up the database. Run the following commands:

```csharp
dotnet tool install --global dotnet-ef
dotnet add package Microsoft.EntityFrameworkCore.Design
dotnet ef migrations add InitialCreate
dotnet ef database update
```

This installs dotnet EntityFramework and the design package, which is required to run the command on a project. The migrations command scaffolds a migration to create the initial set of tables for the model. The database update command creates the database and applies the new migration to it.

Finally, connect to the database, insert a row, query it, and delete it. Copy the following sample code to your `Program.cs` file.

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

### Step 3: Run the application and verify the results

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

## Next steps

- Explore [scaling C# applications](/preview/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop C# applications with YugabyteDB Managed](/preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-csharp/).
