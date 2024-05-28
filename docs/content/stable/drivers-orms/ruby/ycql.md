---
title: YugabyteDB Ruby Driver for YCQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect an application using YugabyteDB Ruby driver for YCQL
menu:
  stable:
    identifier: ycql-ruby-driver
    parent: ruby-drivers
    weight: 420
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-pg/" class="nav-link">
      YSQL
    </a>
  </li>
  <li class="active">
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">
   <li >
    <a href="../ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YugabyteDB Ruby Driver
    </a>
  </li>
</ul>

[Yugabyte Ruby Driver for YCQL](https://github.com/yugabyte/cassandra-ruby-driver) is based on [DataStax Ruby Driver](https://github.com/datastax/ruby-driver) with additional [smart driver](../../smart-drivers-ycql/) features.

{{< note title="YugabyteDB Managed" >}}

To use the driver's partition-aware load balancing feature in a YugabyteDB Managed cluster, applications must be deployed in a VPC that has been peered with the cluster VPC so that they have access to all nodes in the cluster. For more information, refer to [Using YCQL drivers with YugabyteDB Managed](../../smart-drivers-ycql/#using-ycql-drivers-with-yugabytedb-managed).

{{< /note >}}

## Install the YugabyteDB Ruby Driver for YCQL

To install the [YugabyteDB Ruby Driver for YCQL](https://github.com/yugabyte/cassandra-ruby-driver), run the following `gem install` command:

```sh
$ gem install yugabyte-ycql-driver
```

## Create a sample Ruby application

Create a file `yb-ycql-helloworld.rb` and copy the following content to it.

```ruby
require 'ycql'

# Create the cluster connection, connects to localhost by default.
cluster = Cassandra.cluster
session = cluster.connect()

# Create the keyspace.
session.execute('CREATE KEYSPACE IF NOT EXISTS ybdemo;')
puts "Created keyspace ybdemo"

# Create the table.
session.execute(
  """
  CREATE TABLE IF NOT EXISTS ybdemo.employee (id int PRIMARY KEY,
                                              name varchar,
                                              age int,
                                              language varchar);
  """)
puts "Created table employee"

# Insert a row.
session.execute(
  """
  INSERT INTO ybdemo.employee (id, name, age, language)
  VALUES (1, 'John', 35, 'Ruby');
  """)
puts "Inserted (id, name, age, language) = (1, 'John', 35, 'Ruby')"

# Query the row.
rows = session.execute('SELECT name, age, language FROM ybdemo.employee WHERE id = 1;')
rows.each do |row|
  puts "Query returned: %s %s %s" % [ row['name'], row['age'], row['language'] ]
end

# Close the connection.
cluster.close()
```

## Run the application

To use the application, run the following command:

```sh
$ ruby yb-cql-helloworld.rb
```

You should see the following output.

```output
Created keyspace ybdemo
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Ruby')
Query returned: John 35 Ruby
```

## Learn more

- Build Ruby applications using [Pg Gem Driver](../ysql-pg/)
- Build Ruby applications using [Active Record ORM](../activerecord/)
