---
title: YugabyteDB Python Driver for YCQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect an application using YugabyteDB Python driver for YCQL
menu:
  v2.18:
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

#### Use SSL

To run the application with SSL, create the cluster connection with additional SSL imports and parameters described as follows:

```python
# Include additional imports.
from ssl import SSLContext, PROTOCOL_TLS_CLIENT, CERT_REQUIRED
from cassandra.auth import PlainTextAuthProvider

# Include additional parameters.
ssl_context = SSLContext(PROTOCOL_TLS_CLIENT)
ssl_context.load_verify_locations('path to certs file')
ssl_context.verify_mode = CERT_REQUIRED

# Create the cluster connection.
cluster = Cluster(contact_points=['ip_address'],
    ssl_context=ssl_context,
    ssl_options={'server_hostname': 'ip_address'},
    auth_provider=PlainTextAuthProvider(username='username', password='password'))
session = cluster.connect()
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

