---
title: Build a Python App
linkTitle: Build a Python App
description: Build a Python App
block_indexing: true
menu:
  v1.2:
    parent: build-apps
    name: Python
    identifier: python-1
    weight: 553
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/quick-start/build-apps/python/ysql-psycopg2" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - psycopg2
    </a>
  </li>
  <li >
    <a href="/latest/quick-start/build-apps/python/ysql-sqlalchemy" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - SQL Alchemy
    </a>
  </li>
  <li>
    <a href="/latest/quick-start/build-apps/python/ycql" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

## Install the psycopg2 driver

Install the python PostgreSQL driver using the following command. You can get further details for the driver [here](https://pypi.org/project/psycopg2/).

```sh
$ pip install psycopg2-binary
```

## Working Example

### Prerequisites

This tutorial assumes that you have:

- installed YugabyteDB and created a universe with YSQL enabled. If not, please follow these steps in the [Quick Start guide](../../../quick-start/explore-ysql/).


### Writing the python code

Create a file `yb-sql-helloworld.py` and add the following content to it.

```python
import psycopg2

# Create the database connection.                                                                 
conn = psycopg2.connect("host=127.0.0.1 port=5433 dbname=postgres user=postgres password=postgres")

# Open a cursor to perform database operations
# The default mode for psycopg2 is "autocommit=False".
conn.set_session(autocommit=True)
cur = conn.cursor()

# Create the table. (It might pre-exist.)
cur.execute(
  """                                                                                             
  DROP TABLE IF EXISTS employee
  """)

cur.execute(
  """                                                                                             
  CREATE TABLE employee (id int PRIMARY KEY,                                                      
                         name varchar,                                                            
                         age int,                                                                 
                         language varchar)
  """)
print("Created table employee")
cur.close()

# Take advantage of ordinary, transactional behavior for DMLs.
conn.set_session(autocommit=False)
cur = conn.cursor()

# Insert a row.                                                                                   
cur.execute("INSERT INTO employee (id, name, age, language) VALUES (%s, %s, %s, %s)",
            (1, 'John', 35, 'Python'))
print("Inserted (id, name, age, language) = (1, 'John', 35, 'Python')")

# Query the row.                                                                                  
cur.execute("SELECT name, age, language FROM employee WHERE id = 1")
row = cur.fetchone()
print("Query returned: %s, %s, %s" % (row[0], row[1], row[2]))

# Commit and close down. 
conn.commit()                                                                         
cur.close()
conn.close()
```

### Running the application

To run the application, type the following:

```sh
$ python yb-sql-helloworld.py
```

You should see the following output.

```
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
Query returned: John, 35, Python
```
