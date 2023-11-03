---
title: Connect an application
linkTitle: Connect an app
description: Python driver for YCQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.16:
    identifier: ycql-python-driver
    parent: python-drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../yugabyte-psycopg2/" class="nav-link">
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
      YugabyteDB Python Driver
    </a>
  </li>
</ul>

## Install the Yugabyte Python Driver for YCQL

To install the [Yugabyte Python Driver for YCQL](https://github.com/yugabyte/cassandra-python-driver), run the following command:

```sh
$ pip3 install yb-cassandra-driver --install-option="--no-cython"
```

{{< note title="Note">}}
The flag `--no-cython` is necessary on MacOS Catalina and further MacOS releases to avoid a failure while building the `yb-cassandra-driver`.
{{< /note >}}

## Create a sample Python application

### Prerequisites

This tutorial assumes that you have:

- installed YugabyteDB, created a universe, and are able to interact with it using the YCQL shell. If not, follow the steps in [Quick start](../../../../quick-start/).

### Write the sample Python application

Create a file `yb-cql-helloworld.py` and copy the following content into it.

```python
from cassandra.cluster import Cluster

# Create the cluster connection.
cluster = Cluster(['127.0.0.1'])
session = cluster.connect()

# Create the keyspace.
session.execute('CREATE KEYSPACE IF NOT EXISTS ybdemo;')
print("Created keyspace ybdemo")

# Create the table.
session.execute(
  """
  CREATE TABLE IF NOT EXISTS ybdemo.employee (id int PRIMARY KEY,
                                              name varchar,
                                              age int,
                                              language varchar);
  """)
print("Created table employee")

# Insert a row.
session.execute(
  """
  INSERT INTO ybdemo.employee (id, name, age, language)
  VALUES (1, 'John', 35, 'Python');
  """)
print("Inserted (id, name, age, language) = (1, 'John', 35, 'Python')")

# Query the row.
rows = session.execute('SELECT name, age, language FROM ybdemo.employee WHERE id = 1;')
for row in rows:
  print(row.name, row.age, row.language)

# Close the connection.
cluster.shutdown()
```

### Run the application

To run the application, type the following:

```sh
$ python3 yb-cql-helloworld.py
```

You should see the following output.

```output
Created keyspace ybdemo
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
John 35 Python
```
