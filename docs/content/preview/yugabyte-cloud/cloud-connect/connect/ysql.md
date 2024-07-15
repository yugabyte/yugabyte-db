<!--
+++
private = true
+++
-->

To connect to a cluster using `ysqlsh`:

1. On the **Clusters** tab, select a cluster.
1. Click **Connect**.
1. Click **YugabyteDB Client Shell**.
1. Make sure you are running the latest version of the Yugabyte Client shell. If you have not installed Yugabyte Client on your computer, select your operating system and copy the command to install Yugabyte Client from the command line.
1. Click **Download CA Cert** to download the root.crt certificate for TLS encryption, and install the certificate on your computer. If you are using Docker, copy the certificate to your Docker container.
1. If your cluster is deployed in a VPC, choose **Private Address** if you are connecting from a peered VPC. Otherwise, choose **Public Address** (only available if you have enabled Public Access for the cluster; not recommended for production).
1. Copy the **YSQL** connection string.

    The connection string includes flags specifying the host (`host`), username (`user`), database (`dbname`), and TLS settings (`sslmode` and `sslrootcert`). The command specifies that the connection will use the CA certificate you installed on your computer. For information on using other SSL modes, refer to [SSL modes in YSQL](../../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql).

    Here's an example of the generated `ysqlsh` command:

    ```sh
    ./ysqlsh "host=740ce33e-4242-4242-a424-cc4242c4242b.aws.ybdb.io \
    user=<DB USER> \
    dbname=yugabyte \
    sslmode=verify-full \
    sslrootcert=<ROOT_CERT_PATH>"
    ```

1. On your computer, change directories to the directory where you installed the client shell.
1. Paste and run the command, replacing

    - `<DB USER>` with your database username.
    - `yugabyte` with the database name, if you're connecting to a database other than the default (yugabyte).
    - `<ROOT_CERT_PATH>` with the path to the root certificate on your computer.

1. Enter your password when prompted.

The `ysqlsh` shell opens connected to the remote cluster.

```output
ysqlsh (11.2-YB-2.6.1.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.

yugabyte=>
```

<!-- markdownlint-disable-file MD041 -->
