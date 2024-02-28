---
title: PostgreSQL Psycopg2 Driver
headerTitle: Python Drivers
linkTitle: Python Drivers
description: PostgreSQL Psycopg2 Python Driver for YSQL
headcontent: Python Drivers for YSQL
menu:
  v2.18:
    name: Python Drivers
    identifier: ref-postgres-psycopg2-driver
    parent: drivers
    weight: 660
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
    <li >
    <a href="../yugabyte-psycopg2-reference/" class="nav-link ">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      YugabyteDB Psycopg2 Smart Driver
    </a>
  </li>
  <li >
    <a href="../postgres-psycopg2-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Psycopg2 Driver
    </a>
  </li>
</ul>

Psycopg is the most popular PostgreSQL database adapter for the Python programming language. Its main features are the complete implementation of the Python DB API 2.0 specification and the thread safety (several threads can share the same connection). YugabyteDB has full support for [Psycopg2](https://www.psycopg.org/).

## Fundamentals

Learn how to perform common tasks required for Python application development using the PostgreSQL Psycopg2 driver.

### Download the driver dependency

Building Psycopg2 requires a few prerequisites (a C compiler, some development packages). Check the [installation instructions](https://www.psycopg.org/docs/install.html#install-from-source) and the [FAQ](https://www.psycopg.org/docs/faq.html#faq-compile) for details.

If prerequisites are met, you can install psycopg like any other Python package, using pip to download it from [PyPI](https://pypi.org/project/psycopg2/):

```sh
$ pip install psycopg2
```

Or, you can use the setup.py script if you've downloaded the source package locally:

```sh
$ python setup.py build
$ sudo python setup.py install
```

You can also obtain a stand-alone package, not requiring a compiler or external libraries, by installing the [psycopg2-binary](https://pypi.org/project/psycopg2-binary/) package from PyPI:

```sh
$ pip install psycopg2-binary
```

The binary package is a practical choice for development and testing but in production it is recommended to use the package built from sources.

### Connect to YugabyteDB database

Python applications can connect to and query the YugabyteDB database using the following:

- Import the psycopg2 package.

    ```python
    import psycopg2
    ```

- The Connection details can be provided as a string or a dictionary.

    Connection String

    ```python
    "dbname=database_name host=hostname port=port user=username  password=password"
    ```

    Connection Dictionary

    ```python
    user = 'username', password='xxx', host = 'hostname', port = 'port', dbname = 'database_name'
    ```

The following table describes the connection parameters required to connect to the YugabyteDB database

| Parameters | Description | Default |
| :-------------- | :------------------------- | :---------- |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database/dbname | Database name | yugabyte
| user | User for connecting to the database | yugabyte
| password | Password for the user | yugabyte

The following is an example connection string for connecting to YugabyteDB.

```python
conn = psycopg2.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
```

### Create a cursor

To execute any SQL commands, a cursor needs to be created after a connection is made. It allows Python code to execute PostgreSQL commands in a database session. Cursors are created by the `connection.cursor()` method; they are bound to the connection for the entire lifetime, and all the commands are executed in the context of the database session wrapped by the connection.

```python
cur = conn.cursor()
```

### Create tables

Tables can be created in YugabyteDB by passing the `CREATE TABLE` DDL statement to the `cursor.execute(statement)` method, using the following example:

```sql
CREATE TABLE IF NOT EXISTS employee (id int PRIMARY KEY, name varchar, age int, language text)
```

```python
conn = psycopg2.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
cur = conn.cursor()
cur.execute('CREATE TABLE IF NOT EXISTS employee (id int PRIMARY KEY, name varchar, age int, language varchar)')
```

### Read and write data

#### Insert data

To write data into YugabyteDB, execute the `INSERT` statement using the `cursor.execute(statement)` method.

For example:

```sql
INSERT INTO employee VALUES (1, 'John', 35, 'Java')
```

```python
conn = psycopg2.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
cur = conn.cursor()
cur.execute('INSERT INTO employee VALUES (1, 'John', 35, 'Java')')
```

<!-- For inserting data using JDBC clients, it is always a good pratice to use `java.sql.PreparedStatemet` for executing `INSERT` statements.

```java
Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte","yugabyte", "yugabyte");
Statment stmt = conn.createStatement();
try {

  PreparedStatement pstmt = connection.prepareStatement("INSERT INTO employees (id, name, age, langugage) VALUES (?, ?, ?, ?)");
  pstmt.setInt(1, 1);
  pstmt.setString(2, "John");
  pstmt.setInt(3, 35);
  pstmt.setString(4, "Java");
  pstmt.execute();

} catch (SQLException e) {
  System.err.println(e.getMessage());
}
``` -->

#### Query data

To query data from YugabyteDB tables, execute the `SELECT` statement using `cursor.execute(statement)` method followed by `cursor.fetchall()` method. `fetchall()` fetches all the rows of a query result, returning them as a list of tuples. An empty list is returned if there are no records to fetch.

For example:

```sql
SELECT * from employee;
```

```python
conn = psycopg2.connect(dbname='yugabyte',host='localhost',port='5433',user='yugabyte',password='yugabyte')
cur = conn.cursor()
cur.execute('SELECT * from employee')
rows = cur.fetchall()
for row in rows:
  print("\nQuery returned: %s, %s, %s" % (row[0], row[1], row[2]))
```

## Configure SSL/TLS

Psycopg2 supports several SSL modes, as follows:

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Supported |
| prefer (default) | Supported |
| require | Supported |
| verify-ca | Supported |
| verify-full | Supported |

By default, the driver supports the `prefer` SSL mode. And in the `require` mode, a root CA certificate isn't required to be configured.

To enable `verify-ca` or `verify-full`, you need to provide the path to the root CA certificate in the connection string using the `sslrootcert` parameter. The default location is `~/.postgresql/root.crt`. If the root certificate is in a different file, specify it in the `sslrootcert` parameter:

```python
conn = psycopg2.connect("host=<hostname> port=5433 dbname=yugabyte user=<username> password=<password> sslmode=verify-full sslrootcert=/Users/my-user/Downloads/root.crt")
```

The difference between `verify-ca` and `verify-full` depends on the policy of the root CA. If you're using a public CA, verify-ca allows connections to a server that somebody else may have registered with the CA. Because of this behavior, you should always use verify-full with a public CA. If you're using a local CA, or even a self-signed certificate, using verify-ca may provide enough protection, but the best security practice is to always use verify-full.

## Limitations

Currently, [PostgreSQL psycopg2 driver](https://github.com/psycopg/psycopg2) and [Yugabyte Psycopg2 smart driver](https://github.com/yugabyte/psycopg2) _cannot_ be used in the same environment.
