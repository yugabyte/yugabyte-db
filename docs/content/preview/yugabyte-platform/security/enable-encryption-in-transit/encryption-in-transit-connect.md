---
title: Connect clients to universes
headerTitle: Connect clients
linkTitle: Connect clients
description: Connect clients to universes using SSL/TLS.
menu:
  preview_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: eit-connect
    weight: 30
type: docs
---

YugabyteDB Anywhere allows you to protect data in transit by using client-to-server encryption for communication between clients and nodes when using CLIs, tools, and APIs for YSQL and YCQL.

{{< note title="Note" >}}

Client-to-server encryption in transit is not supported for YEDIS. Before you can enable client-to-server encryption, you first must enable server-to-server encryption.

{{< /note >}}

## Connecting to clusters

Using TLS, you can connect to the YSQL and YCQL endpoints.

### Connect to a YSQL endpoint with TLS

If you created your universe with the Client-to-Node TLS option enabled, then you must download client certificates to your client computer to establish connection to your database, as follows:

- Navigate to the **Certificates** page and then to your universe's certificate.

- Click **Actions** and select **Download YSQL Cert**, as shown in the following illustration. This triggers the download of the `yugabytedb.crt` and `yugabytedb.key` files.

  ![download-ysql-cert](/images/yp/encryption-in-transit/download-ysql-cert.png)

- Optionally, when connecting to universes that are configured with custom CA-signed certificates, obtain the root CA and client YSQL certificate from your administrator. These certificates are not available on YugabyteDB Anywhere for downloading.

- For testing with a `ysqlsh` client, paste the `yugabytedb.crt` and `yugabytedb.key` files into the `<home-dir>/.yugabytedb` directory and change the permissions to `0600`, as follows:

  ```sh
  mkdir ~/.yugabytedb; cd ~/.yugabytedb
  cp <DownloadDir>/yugabytedb.crt .
  cp <DownloadDir>/yugabytedb.key .
  chmod 600 yugabytedb.*
  ```

- Run `ysqlsh` using the `sslmode=require` option, as follows:

  ```sh
  cd <yugabyte software install directory>
  bin/ysqlsh -h 172.152.43.78 -p 5433 sslmode=require
  ```

  ```output
  ysqlsh (11.2-YB-2.3.3.0-b0)
  SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
  Type "help" for help.

  yugabyte=#
  ```

To use TLS from a different client, consult the client-specific documentation. For example, if you are using a PostgreSQL JDBC driver to connect to YugabyteDB, see [Configuring the client](https://jdbc.postgresql.org/documentation/head/ssl-client.html) for more details.

If you are using PostgreSQL/YugabyteDB JDBC driver with SSL, you need to convert the certificates to DER format. To do this, you need to perform only steps 6 and 7 from [Set up SSL certificates for Java applications](../../../reference/drivers/java/postgres-jdbc-reference/#set-up-ssl-certificates-for-java-applications) section after downloading the certificates.

### Connect to a YCQL endpoint with TLS

If you created your universe with the Client-to-Node TLS option enabled, then you must download client certificates to your client computer to establish connection to your database, as follows:

- Navigate to the **Certificates** page and then to your universe's certificate.

- Click **Actions** and select **Download Root Cert**, as shown in the following illustration. This triggers the download of the `root.crt` file.

  ![download-root-cert](/images/yp/encryption-in-transit/download-root-cert.png)

- Optionally, when connecting to universes that are configured with custom CA-signed certificates, obtain the root CA and client YSQL certificate from your administrator. These certificates are not available on YugabyteDB Anywhere for downloading.

- Set `SSL_CERTFILE` environment variable to point to the location of the downloaded root certificate.

- Run `ycqlsh` using the `-ssl` option, as follows:

  ```sh
  cp <DownloadDir>/root.crt ~/.yugabytedb/root.crt
  export SSL_CERTFILE=~/.yugabytedb/root.crt
  bin/ycqlsh 172.152.43.78 --ssl
  ```

  ```output
  Connected to local cluster at 172.152.43.78:9042.
  [ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
  Use HELP for help.
  ycqlsh>
  ```

To use TLS from a different client, consult the client-specific documentation. For example, if you are using a Cassandra driver to connect to YugabyteDB, see [SSL](https://docs.datastax.com/en/developer/python-driver/3.19/security/#ssl) .
