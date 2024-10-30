---
title: Ruby PostgreSQL driver
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Ruby application using the Pg Gem Driver for YSQL
menu:
  v2.20:
    identifier: ysql-pg-driver
    parent: ruby-drivers
    weight: 410
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../ysql-pg/" class="nav-link">
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
    <a href="../ysql-pg/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      Pg Gem Driver
    </a>
  </li>
</ul>

## Prerequisites

Install the Ruby PostgreSQL driver (`pg`) using the following command:

```sh
$ gem install pg -- --with-pg-config=<yugabyte-install-dir>/postgres/bin/pg_config
```

For more information on the driver, see the [pg driver documentation](https://deveiate.org/code/pg/).

## Create the application

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

## Run the application

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

## Learn more

- Build Ruby applications using [YugabyteDB Ruby Driver for YCQL](../ycql/)
- Build Ruby applications using [Active Record ORM](../activerecord/)
