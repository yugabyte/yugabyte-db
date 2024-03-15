---
title: JDBC Drivers
linkTitle: JDBC Drivers
description: JDBC Drivers for YSQL
headcontent: JDBC Drivers for YSQL
menu:
  v2.16:
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
      YugabyteDB JDBC Smart Driver
    </a>
  </li>

  <li >
    <a href="../postgres-jdbc-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL JDBC Driver
    </a>
  </li>

</ul>

The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official JDBC driver for PostgreSQL, and can be used for connecting to YugabyteDB YSQL. YugabyteDB YSQL has full compatibility with the PostgreSQL JDBC Driver, allowing Java programmers to connect to YugabyteDB database to execute DMLs and DDLs using the JDBC APIs.

## Download the driver dependency

PostgreSQL JDBC Driver is available as a maven dependency, and you can download the driver by adding the following dependency into the Java project.

### Maven dependency

To get the driver from Maven, add the following dependencies to the Maven project:

```xml
<!-- https://mvnrepository.com/artifact/org.postgresql/postgresql -->
<dependency>
  <groupId>org.postgresql</groupId>
  <artifactId>postgresql</artifactId>
  <version>42.2.14</version>
</dependency>
```

### Gradle dependency

To get the driver, add the following dependencies to the Gradle project:

```java
// https://mvnrepository.com/artifact/org.postgresql/postgresql
implementation 'org.postgresql:postgresql:42.2.14'
```

## Fundamentals

Learn how to perform common tasks required for Java application development using the PostgreSQL JDBC driver.

<!-- * [Connect to YugabyteDB Database](postgres-jdbc-fundamentals/#connect-to-yugabytedb-database)
* [Configure SSL/TLS](postgres-jdbc-fundamentals/#configure-ssl-tls)
* [Create Table](/postgres-jdbc-fundamentals/#create-table)
* [Read and Write Queries](/postgres-jdbc-fundamentals/#read-and-write-queries) -->

### Connect to YugabyteDB database

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

The following table describes the connection parameters required to connect to the YugabyteDB database.

| Parameters | Description | Default |
| :--------- | :---------- | :------ |
| hostname  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte

### Create tables

Create database tables using the `java.sql.Statement` interface, which is used to execute the `CREATE TABLE` DDL statement.

For example:

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

### Read and write data

#### Insert data

To write data to YugabyteDB, execute the `INSERT` statement using the `java.sql.Statement` interface.

For example:

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

#### Query data

To query data in YugabyteDB tables, execute the `SELECT` statement using the `java.sql.statement` interface. Query results are returned using the `java.sql.ResultSet` interface, which can be iterated using the `resultSet.next()` method for reading the data. Refer to [ResultSet](https://docs.oracle.com/javase/7/docs/api/java/sql/ResultSet.html) in the Java documentation.

For example:

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

## Configure SSL/TLS

To build a Java application that communicates securely over SSL, get the root certificate (`ca.crt`) of the YugabyteDB Cluster. If certificates are not generated yet, follow the instructions in [Create server certificates](../../../../secure/tls-encryption/server-certificates/).

Generally, when configuring the Java client to use SSL, all the certificates required for connecting to the database are available on the Classpath of the Java application, or in the default PostgreSQL working directory `(~/.postgresql/)` of the VM or container where the application is hosted.

Example JDBC URL for connecting to a secure YugabyteDB cluster can be seen below.

```java
Connection conn = DriverManager.getConnection(jdbc:postgresql://localhost:5433/yugabyte?ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt", "yugabyte", "yugabyte");
```

| Parameters | Description | Default |
| :--------- | :---------- | :------ |
| ssl  | Enable SSL JDBC Connection | false
| ssl-mode |  SSL mode used for JDBC Connection | require
| sslrootcert | Server CA Certificate | root.crt

### SSL modes

| SSL Mode | Client Driver Behavior | YugabyteDB Support |
| :------- | :--------------------- | :----------------- |
| disable  | SSL Disabled | Supported
| allow    | SSL enabled only if server requires SSL connection | Not supported
| prefer | SSL enabled only if server requires SSL connection | Not supported
| require | SSL enabled for data encryption and Server identity is not verified | Supported
| verify-ca | SSL enabled for data encryption and Server CA is verified | Supported
| verify-full | SSL enabled for data encryption. Both CA and hostname of the certificate are verified | Supported

### JDBC client identity authentication

YugabyteDB cluster can be configured to authenticate the identity of the JDBC clients connecting to the cluster. In such cases, server certificate (`yugabytedb.crt`) and server key (`yugabytedb.key`) are required along with root certificate (`ca.crt`).

#### Set up SSL certificates for Java applications

Steps for configuring the JDBC client for server authentication are as follows:

1. Download the certificate (`yugabytedb.crt`, `yugabytedb.key`, and `ca.crt`) files (see [Copy configuration files to the nodes](../../../../secure/tls-encryption/server-certificates/#copy-configuration-files-to-the-nodes)).

1. If you do not have access to the system `cacerts` Java truststore you can create your own truststore.

    ```sh
    $ keytool -keystore ybtruststore -alias ybtruststore -import -file ca.crt
    ```

    Enter a password when you're prompted to enter one for your keystore.

1. Export the truststore. In the following command, replace `<YOURSTOREPASS>` with your keystore password.

    ```sh
    $ keytool -exportcert -keystore ybtruststore -alias ybtruststore -storepass <YOURSTOREPASS> -file ybtruststore.crt
    ```

1. Convert and export to PEM format with `ybtruststore.pem`.

   ```sh
   $ openssl x509 -inform der -in ybtruststore.crt -out ybtruststore.pem
   ```

1. Verify the `yugabytedb.crt` client certificate with `ybtruststore`.

    ```sh
    $ openssl verify -CAfile ybtruststore.pem -purpose sslclient yugabytedb.crt
    ```

1. Convert the client certificate to DER format.

    ```sh
    $ openssl x509 –in yugabytedb.crt -out yugabytedb.crt.der -outform der
    ```

1. Convert the client key to pk8 format.

    ```sh
    $ openssl pkcs8 -topk8 -inform PEM -in yugabytedb.key -outform DER -nocrypt -out yugabytedb.key.pk8
    ```

#### SSL certificates for a cluster in Kubernetes (Optional)

Steps for configuring the JDBC client for server authentication in a Kubernetes cluster are as follows:

1. Create a minikube cluster by adding `tls.enabled=true` to the command line described in [Quick start](../../../../quick-start/kubernetes/).

   ```sh
   $ kubectl create namespace yb-demo
   $ helm install yb-demo yugabytedb/yugabyte \
   --version {{<yb-version version="v2.16" format="short">}} \
   --set resource.master.requests.cpu=0.5,resource.master.requests.memory=0.5Gi,\
   resource.tserver.requests.cpu=0.5,resource.tserver.requests.memory=0.5Gi,\
   replicas.master=1,replicas.tserver=1,tls.enabled=true --namespace yb-demo
   ```

1. Verify that SSL is enabled using `ysqlsh`.

   ```sh
    $ ysqlsh
    ```

    ```output
    ysqlsh (11.2-YB-2.9.0.0-b0)
    SSL connection (protocol: TLSv1.3, cipher: TLS_AES_256_GCM_SHA384, bits: 256, compression: off)
    Type "help" for help.
    ```

1. Check for the key and certificate files in yb-tserver.

   ```sh
   $ kubectl exec -n yb-demo -it yb-tserver-0 -- bash
   [root@yb-tserver-0 cores]# ls -al /root/.yugabytedb/
   ```

   ```output
   total 4
   drwxrwxrwt 3 root root  140 Oct 22 06:04 .
   dr-xr-x--- 1 root root 4096 Oct 22 06:19 ..
   drwxr-xr-x 2 root root  100 Oct 22 06:04 ..2021_10_22_06_04_46.596961191
   lrwxrwxrwx 1 root root   31 Oct 22 06:04 ..data -> ..2021_10_22_06_04_46.596961191
   lrwxrwxrwx 1 root root   15 Oct 22 06:04 root.crt -> ..data/root.crt
   lrwxrwxrwx 1 root root   21 Oct 22 06:04 yugabytedb.crt -> ..data/yugabytedb.crt
   lrwxrwxrwx 1 root root   21 Oct 22 06:04 yugabytedb.key -> ..data/yugabytedb.key
   ```

1. Download these files to your system and proceed to step 2 under [Set up SSL certificates](#set-up-ssl-certificates-for-java-applications).

   ```sh
   % mkdir YBClusterCerts; cd YBClusterCerts
   % kubectl exec -n "yb-demo" "yb-tserver-0" -- tar -C "/root/.yugabytedb" -cf - . |tar xf -
   Defaulted container "yb-tserver" out of: yb-tserver, yb-cleanup
   % ls
   root.crt yugabytedb.crt yugabytedb.key
   ```

#### Copy SSL certificates

1. Create an `ssl` resource directory in your java application using the following command:

   ```sh
   $ mkdir -p src/main/resources/ssl
   ```

1. Copy the `yugabytedb.crt.der` and `yugabytedb.key.pk8` certificates into the `ssl` directory.

Update the connection string used by `DriverManager.getConnection` to include the ssl certificates.

The following is an example JDBC URL for connecting to a secure YugabyteDB cluster:

```java
Connection conn = DriverManager.getConnection(jdbc:postgresql://localhost:5433/yugabyte?ssl=true&sslmode=verify-full&sslcert=src/main/resources/ssl/yugabytedb.crt.der&sslkey=src/main/resources/ssl/yugabytedb.key.pk8", "yugabyte", "yugabyte");
```

| Parameters | Description | Default |
| :--------- | :---------- | :------ |
| ssl  | Enable SSL JDBC Connection | false
| ssl-mode |  SSL mode used for JDBC Connection | require
| sslcert | Client SSL Certificate | yugabytedb.crt
| sslkey | Client SSL Certificate Key | yugabytedb.key

## Transaction and isolation levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB supports different [isolation levels](../../../../architecture/transactions/isolation-levels/) for maintaining strong consistency for concurrent data access.

JDBC Driver `java.sql.Connection` interface provides `connection.setAutoCommit()`, `connection.commit()` and `connection.rollback()` methods for enabling transactional access to YugabyteDB Database.

For example:

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
