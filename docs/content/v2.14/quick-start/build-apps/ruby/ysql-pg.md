---
title: Build a Ruby application that uses YSQL
headerTitle: Build a Ruby application
linkTitle: Ruby
description: Build a Ruby application that uses Ruby PostgreSQL driver and YSQL.
menu:
  v2.14:
    parent: build-apps
    name: Ruby
    identifier: ruby-1
    weight: 553
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./ysql-pg.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PG Gem
    </a>
  </li>
  <li >
    <a href="{{< relref "./ysql-rails-activerecord.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - ActiveRecord
    </a>
  </li>
  <li>
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

## Install the pg driver

Install the Ruby PostgreSQL driver (`pg`) using the following command. You can get further details for the driver in the [pg driver documentation](https://deveiate.org/code/pg/).

```sh
$ gem install pg -- --with-pg-config=<yugabyte-install-dir>/postgres/bin/pg_config
```

## Create the sample Ruby application

### Prerequisites

This tutorial assumes that you have:

- installed YugabyteDB and created a universe with YSQL enabled. If not, follow the steps in the [Quick Start guide](/preview/tutorials/quick-start/).

### Add the sample Ruby application code

Create a file `yb-sql-helloworld.rb` and add the following content to it.

```python
#!/usr/bin/env ruby

require 'pg'

begin
  # Output a table of current connections to the DB
  conn = PG.connect(host: '127.0.0.1', port: '5433', dbname: 'yugabyte', user: 'yugabyte', password: 'yugabyte')

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

rescue PG::Error => e
  puts e.message
ensure
  rs.clear if rs
  conn.close if conn
end
```

### Run the application

To use the application, run the following command:

```sh
$ ./yb-sql-helloworld.rb
```

You should see the following output.

```output
Created table employee
Inserted data (1, 'John', 35, 'Ruby')
Query returned: John 35 Ruby
```
