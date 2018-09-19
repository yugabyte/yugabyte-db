
## Installation

Install the python driver using the following command.

```
pip install cassandra-driver
```

## Working Example

### Pre-requisites

This tutorial assumes that you have:

- installed YugaByte DB, created a universe and are able to interact with it using the CQL shell. If not, please follow these steps in the [quick start guide](../../../quick-start/test-cassandra/).


### Writing the python code

Create a file `yb-cql-helloworld.py` and add the following content to it.

```python
from cassandra.cluster import Cluster

# Create the cluster connection.
cluster = Cluster(['127.0.0.1'])
session = cluster.connect()

# Create the keyspace.
session.execute('CREATE KEYSPACE IF NOT EXISTS ybdemo;')
print "Created keyspace ybdemo"

# Create the table.
session.execute(
  """
  CREATE TABLE IF NOT EXISTS ybdemo.employee (id int PRIMARY KEY,
                                              name varchar,
                                              age int,
                                              language varchar);
  """)
print "Created table employee"

# Insert a row.
session.execute(
  """
  INSERT INTO ybdemo.employee (id, name, age, language)
  VALUES (1, 'John', 35, 'NodeJS');
  """)
print "Inserted (id, name, age, language) = (1, 'John', 35, 'Python')"

# Query the row.
rows = session.execute('SELECT name, age, language FROM ybdemo.employee WHERE id = 1;')
for row in rows:
  print row.name, row.age, row.language

# Close the connection.
cluster.shutdown()
```

### Running the application

To run the application, type the following:

```sh
python yb-cql-helloworld.py
```

You should see the following output.

```sh
Created keyspace ybdemo
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
John 35 Python
```
