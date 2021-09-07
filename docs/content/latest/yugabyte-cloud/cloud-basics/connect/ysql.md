To connect to a cluster using `ysqlsh`:

1. On the **Clusters** tab, select a cluster.
1. Click **Connect**.
1. Click **YugabyteDB Client Shell**.
1. If you have not installed the client shell on your computer, select your operating system and copy the command to install the shell from the command line.
1. Click **Download CA Cert** to download the root.crt certificate for TLS encryption, and install the certificate on your computer.
1. Copy the **YSQL** connection string.
    \
    The connection string includes flags specifying the host (`host`), username (`user`), database (`dbname`), and TLS settings (`sslmode` and `sslrootcert`). The command specifies that the connection will use the CA certificate you installed on your computer.

    Here's an example of the generated `ysqlsh` command:

    ```sh
    ./ysqlsh "host=740ce33e-4242-4242-a424-cc4242c4242b.cloud.yugabyte.com \
    user=<DB USER> \
    dbname=<database name> \
    sslmode=verify-full \
    sslrootcert=<path to the CA Cert file>"
    ```

1. On your computer, change directories to the directory where you installed the client shell.
1. Paste and run the command, replacing 

    - `<DB USER>` with your database username.
    - `<database name>` with the name of the database (the default name is `yugabyte`).
    - `<path to the CA Cert file>` with the path to the location where you installed the certificate on your computer.

The `ysqlsh` shell opens connected to the remote cluster.

```output
ysqlsh (11.2-YB-2.1.0.0-b0)
Type "help" for help.

yugabyte=#
```
