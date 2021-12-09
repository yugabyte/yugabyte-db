# Postgres JDBC Fundamentals

## Connect to YugabyteDB Database

You can connect to and query the YugabyteDB database using the `DriverManager` class.

Use the `DriverManager.getConnection` for getting connection object for the YugabyteDB Database which can be used for performing DDLs and DMLs against the database.

JDBC Connection String

```java
jdbc://postgresql://hostname:port/database
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

## Configure SSL/TLS

To build a Java application that communicate securily over SSL with YugabyteDB database, you need the root certificate (`ca.crt`), and node certificate (`yugabytedb.crt`) and key (`yugabytedb.key`) files. If you have not generated these files, follow the instructions in [Create server certificates](../../../../secure/tls-encryption/server-certificates/).

### Set up SSL certificates for Java applications

To build a Java application that connects to YugabyteDB over an SSL connection, you need the root certificate (`ca.crt`), and node certificate (`yugabytedb.crt`) and key (`yugabytedb.key`) files. If you have not generated these files, follow the instructions in [Create server certificates](../../../../secure/tls-encryption/server-certificates/).

1. Download the certificate (`yugabytedb.crt`, `yugabytedb.key`, and `ca.crt`) files (see [Copy configuration files to the nodes](../../../../secure/tls-encryption/server-certificates/#copy-configuration-files-to-the-nodes)).

1. If you do not have access to the system `cacerts` Java truststore you can create your own truststore.

    ```sh
    $ keytool -keystore ybtruststore -alias ybtruststore -import -file ca.crt
    ```

1. Verify the `yugabytedb.crt` client certificate with `ybtruststore`.

    ```sh
    $ openssl verify -CAfile ca.crt -purpose sslclient tlstest.crt
    ```

1. Convert the client certificate to DER format.

    ```sh
    $ openssl x509 –in yugabytedb.crt -out yugabytedb.crt.der -outform der
    ```

1. Convert the client key to pk8 format.

    ```sh
    $ openssl pkcs8 -topk8 -inform PEM -in yugabytedb.key -outform DER -nocrypt -out yugabytedb.key.pk8
    ```

### Client Side Authentication

### SSL Modes

| JDBC Params | Description | Default |
| :----- | :------------ | :----------- |
| hostname  | hostname of the YugabyteDB instance | localhost

## Setting up the Table

## Read and Write Queries
