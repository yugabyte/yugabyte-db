---
title: YugabyteDB Ruby-pg Smart Driver
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Ruby application using YugabyteDB Ruby-pg Smart Driver for YSQL
aliases:
  - /develop/client-drivers/ruby/
  - /stable/develop/client-drivers/ruby/
  - /stable/develop/build-apps/ruby/
  - /stable/quick-start/build-apps/ruby/
menu:
  stable_develop:
    identifier: ruby-pg-driver-1-yb
    parent: ruby-drivers
    weight: 410
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../yb-ruby-pg/" class="nav-link">
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
    <a href="../yb-ruby-pg/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB ruby-pg Smart Driver
    </a>
  </li>
  <li >
    <a href="../ruby-pg/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      Ruby-pg Driver
    </a>
  </li>
</ul>

The [YugabyteDB ruby-pg smart driver](https://rubygems.org/gems/yugabytedb-ysql) is a Ruby driver for [YSQL](/stable/api/ysql/) based on [ged/ruby-pg](https://github.com/ged/ruby-pg), with additional [connection load balancing](../../smart-drivers/) features.

The driver makes an initial connection to the first contact point provided by the application to discover all the nodes in the cluster. If the driver discovers stale information (by default, older than 5 minutes), it refreshes the list of live endpoints with the next connection attempt.

{{< note title="YugabyteDB Aeon" >}}

To use smart driver load balancing features when connecting to clusters in YugabyteDB Aeon, applications must be deployed in a VPC that has been peered with the cluster VPC. For applications that access the cluster from outside the VPC network, either keep the load balancing features disabled (default setting) or use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. For more information, refer to [Using smart drivers with YugabyteDB Aeon](../../smart-drivers/#using-smart-drivers-with-yugabytedb-aeon).

{{< /note >}}

## CRUD operations

The following sections demonstrate how to perform common tasks required for Ruby application development using the YugabyteDB ruby-pg smart driver APIs.

### Prerequisites

Install the YugabyteDB ruby-pg smart driver (`yugabytedb-ysql` gem) using the following command:

```sh
$ gem install yugabytedb-ysql -- --with-pg-config=<yugabyte-install-dir>/postgres/bin/pg_config
```

### Step 1: Import the driver package

Import the YugabyteDB ruby-pg driver module by adding the following import statement in your Ruby code:

```ruby
#!/usr/bin/env ruby

require 'ysql'
```

### Step 2: Set up the database connection

Ruby applications can connect to the YugabyteDB database using the `YSQL.connect()` APIs. The `ysql` module includes all the common functions or structs required for working with YugabyteDB.

Use the `YSQL.connect()` method to create a connection object for the YugabyteDB database. This can be used to perform DDLs and DMLs against the database.

The following table describes the connection parameters required to connect, including [smart driver parameters](../../smart-drivers/) for uniform and topology load balancing.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| host | Host name of the YugabyteDB instance. You can also enter [multiple addresses](#use-multiple-addresses). | localhost |
| port |  Listen port for YSQL | 5433 |
| user | User connecting to the database | yugabyte |
| password | User password | yugabyte |
| dbname | Database name | yugabyte |
| load_balance | Enables [uniform load balancing](../../smart-drivers/#cluster-aware-load-balancing) | false |
| topology_keys | Enables [topology-aware load balancing](../../smart-drivers/#topology-aware-load-balancing). Specify comma-separated geo-locations in the form `cloud.region.zone:priority`. Ignored if `load_balance` is false. | Empty |
| yb_servers_refresh_interval | The interval (in seconds) to refresh the servers list; ignored if `load_balance` is false | 300 |
| fallback_to_topology_keys_only | If set to true and `topology_keys` are specified, the driver only tries to connect to nodes specified in `topology_keys` | false |
| failed_host_reconnect_delay_secs | Time (in seconds) to wait before trying to connect to failed nodes. When the driver is unable to connect to a node, it marks the node as failed using a timestamp, and ignores the node when trying new connections until this time elapses. | 5 |

The `load_balance` property supports the following additional values: any (alias for 'true'), only-primary, only-rr, prefer-primary, and prefer-rr. See [Node type-aware load balancing](../../smart-drivers/#node-type-aware-load-balancing).

The following is an example of enabling uniform load balancing via connection string:

```sh
yburl = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte?load_balance=true"
connection = YSQL.connect(yburl)
```

The following is a code snippet for connecting to YugabyteDB by specifying the connection properties as key-value pairs:

```ruby
connection = YSQL.connect(host: 'localhost', port: '5433', dbname: 'yugabyte',
                                  user: 'yugabyte', password: 'yugabyte',
                                  load_balance: 'true', yb_servers_refresh_interval: '10')
```

The following is an example connection string for connecting to YugabyteDB with topology-aware load balancing, and including a fallback placement:

```sh
yburl = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte?load_balance=true&topology_keys=cloud1.region1.zone1:1,cloud1.region1.zone2:2"
```

After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load balances subsequent connection requests across these servers.

#### Use multiple addresses

You can specify multiple hosts in the connection string to provide alternative options during the initial connection in case one of the nodes is unavailable.

{{< tip title="Tip">}}
To obtain a list of available hosts, you can connect to any cluster node and use the `yb_servers()` YSQL function.
{{< /tip >}}

Delimit the addresses using commas, as follows:

```sh
yburl = "postgresql://yugabyte:yugabyte@127.0.0.1:5433,127.0.0.2:5433,127.0.0.3:5433/yugabyte?load_balance=true"
connection = YSQL.connect(yburl)
```

#### Use SSL

For a YugabyteDB Aeon cluster, or a YugabyteDB cluster with SSL/TLS enabled, set the following SSL-related properties while creating a connection.

| Parameter   | Description |
| :---------- | :---------- |
| sslmode     | SSL mode used for the connection ('prefer', 'require', 'verify-ca', 'verify-full') |
| sslrootcert | Path to the root certificate on your computer. For YugabyteDB Aeon, this would be the CA certificate file downloaded as `root.crt`. |

For example:

```ruby
  conn = YSQL.connect(host: '127.0.0.1', port: '5433', dbname: 'yugabyte', user: 'yugabyte',
             password: 'yugabyte', load_balance: 'true',
             sslmode: 'verify-full', sslrootcert: '/home/centos/root.crt')
```

### Step 3: Write your application

Create a file `yb-sql-helloworld.rb` and add the following content:

```ruby
#!/usr/bin/env ruby

require 'ysql'

begin
  # If you have a read-replica cluster and want to balance connections
  # across only read replica nodes, set the load_balance property to 'only-rr'.
  # Setting yb_servers_refresh_interval to '0' ensures that the driver refreshes
  # the list of server before every connection attempt.
  conn = YSQL.connect(host: '127.0.0.1', port: '5433', dbname: 'yugabyte', user: 'yugabyte',
             password: 'yugabyte', load_balance: 'true',
             yb_servers_refresh_interval: '0')

  # Create table
  conn.exec ("CREATE TABLE employee (id int PRIMARY KEY, \
                                     name varchar, age int, \
                                     language varchar)");

  puts "Created table employee\n";

  # Insert a row
  conn.exec ("INSERT INTO employee (id, name, age, language) \
                            VALUES (1, 'John', 35, 'Ruby')");
  puts "Inserted data (1, 'John', 35, 'Ruby')\n";

  # Query the row
  rs = conn.exec ("SELECT name, age, language FROM employee WHERE id = 1");
  rs.each do |row|
    puts "Query returned: %s %s %s" % [ row['name'], row['age'], row['language'] ]
  end

rescue YSQL::Error => e
  puts e.message
ensure
  rs.clear if rs
  conn.close if conn
end
```

## Run the application

Run the application `yb-sql-helloworld.rb` using the following command:

```sh
./yb-sql-helloworld.rb
```

You should see the following output.

```output
Created table employee
Inserted data (1, 'John', 35, 'Ruby')
Query returned: John 35 Ruby
```

## Learn more

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)
