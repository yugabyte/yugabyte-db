---
title: Connect to clusters
linkTitle: Connect to clusters
description: Connect to clusters
headcontent:
image: /images/section_icons/deploy/enterprise.png
beta: /faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    identifier: connect-to-clusters
    parent: yugabyte-cloud
    weight: 645
isTocNested: true
showAsideToc: true
---

You can connect to YugabyteDB clusters in Yugabyte Cloud with YugabyteDB clients and third party clients.

## YugabyteDB CLIs

You can use the following YugabyteDB clients (locally installed) to connect to your remote clusters in Yugabyte Cloud:

- [YSQL shell (`ysqlsh`)](../../../admin/ysqlsh/)
- [YCQL shell (`cqlsh`)](../../../admin/cqlsh/)

{{< note title="Note" >}}

To use the YugabyteDB CLIs to connect to your remote Yugabyte Cloud clusters, you must have a local installation of
YugabyteDB. If you do not have a local installation, see [Install YugabyteDB](../../../quick-start/install/).

{{< /note >}}

### Connect using the YSQL shell

1. Log into Yugabyte Cloud and click **Cluster** in the navigation bar. The list of available clusters appears.
2. Click **Go to cluster** for your cluster. The Yugabyte Cloud Console appears.
3. Click **Connect** to get the user credentials you need to access the remote cluster. The Connect dialog appears with user credentials
   use **COPY** to copy a generated command that you can use to access the YCQL shell. The generated command includes options specifying
   the host (`-h`), port (`-p`), username (`-U`), and database (`-d`). To specify the password The generated command connects to the default database (`yugabyte`).

   Here's an example of the generated command:

    ```sh
    PGPASSWORD=sx73qlpc ./bin/ysqlsh -h 35.236.85.97 -p 12201 -U admin -d yugabyte
    ```

4. Change directories to the Yugabyte home directory.
5. Paste and run the generated command saved in step 2.

    ```sh
    $ PGPASSWORD=sx73qlpc ./bin/ysqlsh -h 35.236.85.97 -p 12201 -U admin -d yugabyte
    ```

    ```
    ysqlsh (11.2-YB-2.1.0.0-b0)
    Type "help" for help.

    yugabyte=#
    ```

The local YSQL opens connected to the remote cluster.

### Connect using the YCQL shell

1. Log into Yugabyte Cloud and click **Cluster** in the navigation bar. The list of available clusters appears.
2. Click **Go to cluster** for your cluster. The Yugabyte Cloud Console appears.
3. Click **Connect** to get the user credentials you need to access the remote cluster. The Connect dialog appears with user credentials
   use **COPY** to copy a generated command that you can use to access the YCQL shell. The generated command includes options specifying
   the host (`-h`), port (`-p`), username (`-U`), and database (`-d`). To specify the password The generated command connects to the default database (`yugabyte`).

    Here's an example of the generated command:

    ```sh
    ./bin/cqlsh 35.236.85.97 12200 -u admin -p sx73qlpc
    ```

4. Change directories to the Yugabyte home directory.
5. Paste and run the generated command you copied in step 3.

    ```sh
    $ ./bin/cqlsh 35.236.85.97 12200 -u admin -p sx73qlpc
    ```

    ```
    Connected to local cluster at 35.236.85.97:12200.
    [cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
    Use HELP for help.
    admin@cqlsh>
    ```

The local YCQL shell opens connected to the remote cluster.
