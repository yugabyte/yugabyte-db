---
title: JDBC Drivers
linkTitle: JDBC Drivers
description: JDBC Drivers for YSQL
headcontent: JDBC Drivers for YSQL
menu:
  v2.14:
    name: JDBC Drivers
    identifier: ref-postgres-jdbc-driver
    parent: drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../yugabyte-jdbc-reference/" class="nav-link">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      YugabyteDB JDBC Driver
    </a>
  </li>

  <li >
    <a href="../postgres-jdbc-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL JDBC Driver
    </a>
  </li>

</ul>

The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official JDBC driver for PostgreSQL which can used for connecting to YugabyteDB YSQL. YugabyteDB YSQL has full compatibility with PostgreSQL JDBC Driver, allows Java programmers to connect to YugabyteDB database to execute DMLs and DDLs using the JDBC APIs.

## Quick Start

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/java/ysql-jdbc) in the Quick Start section.

## Download the Driver Dependency

Postgres JDBC Drivers are available as a Maven dependency, and you can download the driver by adding the following dependency into the Java project.

### Maven Depedency

```xml
<!-- https://mvnrepository.com/artifact/org.postgresql/postgresql -->
<dependency>
  <groupId>org.postgresql</groupId>
  <artifactId>postgresql</artifactId>
  <version>42.2.14</version>
</dependency>
```

### Gradle Dependency

```java
// https://mvnrepository.com/artifact/org.postgresql/postgresql
implementation 'org.postgresql:postgresql:42.2.14'
```

## Fundamentals

Learn how to perform common tasks required for Java application development using the PostgreSQL JDBC driver.

### Connect to YugabyteDB Database

Java applications can connect to and query the YugabyteDB database using the `java.sql.DriverManager` class. The `java.sql.*` package includes all the JDBC interfaces required for working with YugabyteDB.

Use the `DriverManager.getConnection` method to create a connection object to perform DDLs and DMLs against the database.

JDBC Connection String

```java
jdbc:postgresql://hostname:port/database
```

Example JDBC URL for connecting to YugabyteDB can be seen below.

```java
Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte","yugabyte", "yugabyte");
```

| JDBC Params | Description | Default |
| :---------- | :---------- | :------ |
| hostname  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte

### Create Table

Create database tables using the `java.sql.Statement` interface, which is used to execute the `CREATE TABLE` DDL statement.

For example

```sql
CREATE TABLE IF NOT EXISTS employee (id int primary key, name varchar, age int, language text)
```

```java
Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte","yugabyte", "yugabyte");
Statement stmt = conn.createStatement();
try {
  stm.execute("CREATE TABLE IF NOT EXISTS employee" +
                    "  (id int primary key, name varchar, age int, language text)");

} catch (SQLException e) {
  System.err.println(e.getMessage());
}
```

`java.sql.Statement` throws the `java.sql.SQLException` exception, which needs to handled in the Java code. Read more on designing [Database schemas and tables](../../../../explore/ysql-language-features/databases-schemas-tables/).

### Read and Write Data

#### Insert Data

To write data to YugabyteDB, execute the `INSERT` statement using the `java.sql.Statement` interface.

For example

```java
INSERT INTO employee VALUES (1, 'John', 35, 'Java')
```

```java
Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte","yugabyte", "yugabyte");
Statement stmt = conn.createStatement();
try {
  String insertStr = "INSERT INTO employee VALUES (1, 'John', 35, 'Java')";
  stmt.execute(insertStr);

} catch (SQLException e) {
  System.err.println(e.getMessage());
}
```

When inserting data using JDBC clients, it is good practice to use `java.sql.PreparedStatement` for executing `INSERT` statements.

```java
Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte","yugabyte", "yugabyte");
Statement stmt = conn.createStatement();
try {

  PreparedStatement pstmt = connection.prepareStatement("INSERT INTO employees (id, name, age, language) VALUES (?, ?, ?, ?)");
  pstmt.setInt(1, 1);
  pstmt.setString(2, "John");
  pstmt.setInt(3, 35);
  pstmt.setString(4, "Java");
  pstmt.execute();

} catch (SQLException e) {
  System.err.println(e.getMessage());
}
```

#### Query Data

To query data in YugabyteDB tables, execute the `SELECT` statement using the `java.sql.statement` interface. Query results are returned using the `java.sql.ResultSet` interface, which can be iterated using the `resultSet.next()` method for reading the data. Refer to [ResultSet](https://docs.oracle.com/javase/7/docs/api/java/sql/ResultSet.html) in the Java documentation.

For example

```sql
SELECT * from employee;
```

```java
Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte","yugabyte", "yugabyte");
Statement stmt = conn.createStatement();
try {

  ResultSet rs = stmt.executeQuery("SELECT * FROM employee");
  while (rs.next()) {
    System.out.println(String.format("Query returned: name = %s, age = %s, language = %s", rs.getString(2), rs.getString(3), rs.getString(4)));
  }

} catch (SQLException e) {
  System.err.println(e.getMessage());
}
```

### Configure SSL/TLS

To build a Java application that communicates securely over SSL, get the root certificate (`ca.crt`) of the YugabyteDB Cluster. If certificates are not generated yet, follow the instructions in [Create server certificates](../../../../secure/tls-encryption/server-certificates/).

Generally, when configuring the Java client to use SSL, all the certificates required for connecting to the database are available on the Classpath of the Java application, or in the default PostgreSQL working directory `(~/.postgresql/)` of the VM or container where the application is hosted.

Example JDBC URL for connecting to a secure YugabyteDB cluster can be seen below.

```java
Connection conn = DriverManager.getConnection(jdbc:postgresql://localhost:5433/yugabyte?ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt", "yugabyte", "yugabyte");
```

| JDBC Params | Description | Default |
| :---------- | :---------- | :------ |
| ssl  | Enable SSL JDBC Connection | false
| ssl-mode |  SSL mode used for JDBC Connection | require
| sslrootcert | Server CA Certificate | root.crt

#### SSL Modes

| SSL Mode | Client Driver Behavior | YugabyteDB Support |
| :------- | :--------------------- | ------------------ |
| disable  | SSL Disabled | supported
| allow    | SSL enabled only if server requires SSL connection | Not supported
| prefer | SSL enabled only if server requires SSL connection | Not supported
| require | SSL enabled for data encryption and Server identity is not verified | supported
| verify-ca | SSL enabled for data encryption and Server CA is verified | Supported
| verify-full | SSL enabled for data encryption. Both CA and hostname of the certificate are verified | Supported

#### JDBC Client Identity Authentication

YugabyteDB cluster can be configured to authenticate the identity of the JDBC clients connecting to the cluster. In such cases, server certificate (`yugabytedb.crt`) and server key (`yugabytedb.key`) are required along with root certificate (`ca.crt`).

Steps for Configuring the JDBC Client for Server authentication,

1. Download the certificate (`yugabytedb.crt`, `yugabytedb.key`, and `ca.crt`) files (see [Copy configuration files to the nodes](../../../../secure/tls-encryption/server-certificates/#copy-configuration-files-to-the-nodes)).

2. If you do not have access to the system `cacerts` Java truststore you can create your own truststore.

    ```sh
    $ keytool -keystore ybtruststore -alias ybtruststore -import -file ca.crt
    ```

3. Verify the `yugabytedb.crt` client certificate with `ybtruststore`.

    ```sh
    $ openssl verify -CAfile ca.crt -purpose sslclient tlstest.crt
    ```

4. Convert the client certificate to DER format.

    ```sh
    $ openssl x509 –in yugabytedb.crt -out yugabytedb.crt.der -outform der
    ```

5. Convert the client key to pk8 format.

    ```sh
    $ openssl pkcs8 -topk8 -inform PEM -in yugabytedb.key -outform DER -nocrypt -out yugabytedb.key.pk8
    ```

Create an `ssl` resource directory in your java application and copy over all the certificates. Update the connection string used by `DriverManager.getConnection` to include the ssl certificates.

Example JDBC URL for connecting to Secure YugabyteDB cluster can be seen below.

```java
Connection conn = DriverManager.getConnection(jdbc:postgresql://localhost:5433/yugabyte?ssl=true&sslmode=verify-full&sslcert=src/main/resources/ssl/yugabytedb.crt.der&sslkey=src/main/resources/ssl/yugabytedb.key.pk8", "yugabyte", "yugabyte");
```

| JDBC Params | Description | Default |
| :---------- | :---------- | :------ |
| ssl  | Enable SSL JDBC Connection | false
| ssl-mode |  SSL mode used for JDBC Connection | require
| sslcert | Client SSL Certificate | yugabytedb.crt
| sslkey | Client SSL Certificate Key | yugabytedb.key

### Transaction and Isolation Levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB supports different [isolation levels](../../../../architecture/transactions/isolation-levels/) for maintaining strong consistency for concurrent data access.

JDBC Driver `java.sql.Connection` interface provides `connection.setAutoCommit()`, `connection.commit()` and `connection.rollback()` methods for enabling transactional access to YugabyteDB Database.

For example

```Java
Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte","yugabyte", "yugabyte");
conn.setAutoCommit(false);
Statement stmt = conn.createStatement();
try {

  PreparedStatement pstmt = connection.prepareStatement("INSERT INTO employees (id, name, age, language) VALUES (?, ?, ?, ?)");
  pstmt.setInt(1, 1);
  pstmt.setString(2, "John");
  pstmt.setInt(3, 35);
  pstmt.setString(4, "Java");
  pstmt.execute();

  conn.commit();

} catch (SQLException e) {
  System.err.println(e.getMessage());
}

```

By default PostgreSQL JDBC driver will have `auto-commit` mode enabled which means each SQL statement is treated as a transaction and is automatically committed. If one or more SQL statements are encapsulated in a transaction, auto-commit mode must be disabled by using `setAutoCommit()` method of the `Connection` object.
