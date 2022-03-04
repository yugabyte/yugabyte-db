---
title: Python Drivers
linkTitle: Python Drivers
description: Python Drivers for YSQL
headcontent: Python Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    name: Python Drivers
    identifier: postgres-psycopg2-driver
    parent: python-drivers
    weight: 500
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/drivers-orms/python/yugabyte-psycopg2" class="nav-link active">
      <i class="icon-java-bold" aria-hidden="true"></i>
      YugabyteDB Psycopg2
    </a>
  </li>

  <li >
    <a href="/latest/drivers-orms/python/postgres-psycopg2" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Psycopg2
    </a>
  </li>

</ul>

Psycopg is the most popular PostgreSQL database adapter for the Python programming language. Its main features are the complete implementation of the Python DB API 2.0 specification and the thread safety (several threads can share the same connection). YugabyteDB has full support for [Psycopg2](https://www.psycopg.org/).

## Step 1: Download the Driver Dependency

Building Psycopg requires a few prerequisites (a C compiler, some development packages): please check the [install](https://www.psycopg.org/docs/install.html#install-from-source) and the [faq](https://www.psycopg.org/docs/faq.html#faq-compile) documents in the doc dir or online for the details.

If prerequisites are met, you can install psycopg like any other Python package, using ``pip`` to download it from [PyPI](https://pypi.org/project/psycopg2/):
```
$ pip install psycopg2
```
or using ``setup.py`` if you have downloaded the source package locally:
```
$ python setup.py build
$ sudo python setup.py install
```
You can also obtain a stand-alone package, not requiring a compiler or external libraries, by installing the [psycopg2-binary](https://pypi.org/project/psycopg2-binary/) package from PyPI:
```
$ pip install psycopg2-binary
```
The binary package is a practical choice for development and testing but in production it is advised to use the package built from sources.

## Step 2: Connect to your Cluster

Python Apps can connect to and query the YugabyteDB database. To do that first import the psycopg2 package. 
```python
import psycopg2
```
The Connection details can be provided as a string or a dictionary.
Connection String

```python
"dbname=database_name host=hostname port=port user=username  password=password"
```
Connection Dictionary
```python
user = 'username', password='xxx', host = 'hostname', port = 'port', dbname = 'database_name'
```

Example URL for connecting to YugabyteDB can be seen below.

```python
conn = psycopg2.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
```

| Params | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database/dbname | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte

Example URL for connecting to YugabyteDB cluster enabled with on the wire SSL encryption.

```python
conn = psycopg2.connect("host=<hostname> port=5433 dbname=yugabyte user=<username> password=<password> sslmode=verify-full sslrootcert=/Users/my-user/Downloads/root.crt")
```

| Params | Description | Default |
| :---------- | :---------- | :------ |
| sslmode | SSL mode  | prefer
| sslrootcert | path to the root certificate on your computer | ~/.postgresql/

If you have created Free tier cluster on [Yugabyte Anywhere](https://www.yugabyte.com/cloud/), [Follow the steps](/latest/yugabyte-cloud/cloud-connect/connect-applications/) to download the Credentials and SSL Root certificate.

## Step 3: Query the YugabyteDB Cluster from Your Application

Next, Create a new Python file called `QuickStartApp.py` in the base package directory of your project. Copy the sample code below in order to setup a YugbyteDB Tables and query the Table contents from the java client. Ensure you replace the connection string `yburl` with credentials of your cluster and SSL certs if required.
```python
import psycopg2

# Create the database connection.

yburl = "host=127.0.0.1 port=5433 dbname=yugabyte user=yugabyte password=yugabyte"

conn = psycopg2.connect(yburl)

# Open a cursor to perform database operations.
# The default mode for psycopg2 is "autocommit=false".

conn.set_session(autocommit=True)
cur = conn.cursor()

# Create the table. (It might preexist.)

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
When you run the Project, `QuickStartApp.py` should output something like below:

```text
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
Query returned: John, 35, Python
```

if you receive no output or error, check whether you included the proper connection string with the right credentials.

After completing these steps, you should have a working Python app that uses Psycopg2 for connecting to your cluster, setup tables, run query and print out results.

## Next Steps

- [Django example](/latest/drivers-orms/python/django/)
- [SQLAlchemy example](/latest/drivers-orms/python/sqlalchemy/)
