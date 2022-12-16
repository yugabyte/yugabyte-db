<!--
+++
private = true
+++
-->

To connect to a cluster using `ycqlsh`:

1. On the **Clusters** tab, select a cluster.
1. Click **Connect**.
1. Click **YugabyteDB Client Shell**.
1. Make sure you are running the latest version of the Yugabyte Client shell. If you have not installed Yugabyte Client on your computer, select your operating system and copy the command to install Yugabyte Client from the command line.
1. Click **Download CA Cert** to download the root.crt certificate for TLS encryption, and install the certificate on your computer. If you are using Docker, copy the certificate to your Docker container.
1. If your cluster is deployed in a VPC, choose **Private Address** if you are connecting from a peered VPC. Otherwise, choose **Public Address** (only available if you have enabled Public Access for the cluster; not recommended for production).
1. Copy the **YCQL** connection string.

    The connection string includes the cluster host and port, with flags for the database username (`-u`), and TLS settings (`--ssl`). The command specifies that the connection will use the CA certificate you installed on your computer.

    Here's an example of the generated `ycqlsh` command:

    ```sh
    SSL_CERTFILE=<ROOT_CERT_PATH> \
    ./ycqlsh \
    740ce33e-4242-4242-a424-cc4242c4242b.aws.ybdb.io 9042 \
    -u <DB USER> \
    --ssl
    ```

1. On your computer, change directories to the directory where you installed the client shell.
1. Paste and run the command, replacing

    - `<ROOT_CERT_PATH>` with the path to the root certificate on your computer.
    - `<DB USER>` with your database username.

The `ycqlsh` shell opens connected to the remote cluster.

```output
Connected to local cluster at 35.236.85.97:12200.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
admin@ycqlsh>
```

<!-- markdownlint-disable-file MD041 -->
