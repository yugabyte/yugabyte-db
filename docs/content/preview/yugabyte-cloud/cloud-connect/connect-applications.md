---
title: Connect applications
linkTitle: Connect applications
description: Connect applications to YugabyteDB Managed clusters
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  preview_yugabyte-cloud:
    identifier: connect-applications
    parent: cloud-connect
    weight: 30
type: docs
---

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). Before you can connect an application, you need to install the correct driver.

YugabyteDB Managed clusters have SSL (encryption in-transit) enabled so make sure your driver details include SSL parameters.

For examples of applications that connect to YugabyteDB Managed using common drivers, refer to [Build an application](../../../develop/build-apps/).

For examples of connecting applications to YugabyteDB Managed, refer to [Example applications](../../cloud-examples/).

{{< warning title="IP allow list" >}}

Before you can connect, your application has to be able to reach your YugabyteDB Managed cluster. To enable inbound network access from your application environment to a cluster, you need to add the public IP addresses to the cluster [IP allow list](../../cloud-secure-clusters/add-connections).

If you are using [VPC peering](../../cloud-basics/cloud-vpcs/), you need to add the public IP addresses of the peered application VPC to the cluster IP allow list.

{{< /warning >}}

## Connect an application

To connect a cluster to an application:

1. On the **Clusters** tab, select the cluster.
1. Click **Connect**.
1. Click **Connect to your Application**.
1. Click **Download CA Cert** and install the certificate on the computer running the application.
1. Choose the API used by your application - YSQL or YCQL.

    - Choosing YSQL displays a connection string you can add to your application.

    - Choosing YCQL displays connection parameters that you will use to connect your application.

### YSQL

YSQL applications can use the connection string to connect. Here's an example of a generated `ysqlsh` string:

```sh
postgresql://<DB USER>:<DB PASSWORD>@4242424.aws.ybdb.io:5433/yugabyte? \
ssl=true& \
sslmode=verify-full& \
sslrootcert=<ROOT_CERT_PATH>
```

Add the string to your application, replacing

- `<DB USER>` with your database username.
- `<DB PASSWORD>` with your database password.
- `yugabyte` with the database name, if you're connecting to a database other than the default (yugabyte).
- `<ROOT_CERT_PATH>` with the path to the root certificate on your computer.

For example:

```sh
postgresql://admin:qwerty@4242424.aws.ybdb.io:5433/yugabyte?ssl=true& \
sslmode=verify-full&sslrootcert=~/.postgresql/root.crt
```

The connection string includes parameters for TLS settings (`ssl`, `sslmode` and `sslrootcert`). The generated `ysqlsh` connection string uses the `verify-full` SSL mode by default.

If you're connecting to a Hasura Cloud project, which doesn't use the CA certificate, select **Optimize for Hasura Cloud** to modify the string. Before using the string to connect in a Hasura project, be sure to encode any special characters. For an example of connecting a Hasura Cloud project to YugabyteDB Managed, refer to [Connect Hasura Cloud to YugabyteDB Managed](../../cloud-examples/hasura-cloud/).

For information on using other SSL modes, refer to [SSL modes in YSQL](../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql).

### YCQL

To connect a YCQL application, use the connection parameters in your application to connect to your cluster. The parameters are:

- LocalDatacenter - The name of the local datacenter for the cluster.
- Host - The cluster host name.
- Port - The port number of the YCQL client API on the YugabyteDB database (9042).

To connect your application, do the following:

- Download the CA certificate.
- Add the YCQL java driver to your dependencies.
- Initialize SSLContext using the downloaded root certificate.

For an example of building a Java application connected to YugabyteDB Managed using the Yugabyte Java Driver for YCQL v4.6, refer to [Connect a YCQL Java application](../../cloud-examples/connect-ycql-application/).

<!--
## Run the sample application

YugabyteDB Managed comes configured with a sample application that you can use to test your cluster.

Before you can connect from your computer, you must add the IP address of the computer to an IP allow list, and the IP allow list must be assigned to the cluster. Refer to [Assign IP Allow Lists](../add-connections/).

You will also need Docker installed on you computer.

To run the sample application:

1. On the **Clusters** tab, select a cluster.
1. Click **Connect**.
1. Click **Run a Sample Application**.
1. Copy the connect string for YSQL or YCQL.
1. Run the command in docker from your computer, replacing `<path to CA cert>`, `<db user>`, and `<db password>` with the path to the CA certificate for the cluster and your database credentials.
-->

## Learn more

- [Add database users](../../cloud-secure-clusters/add-users/)
- [Build an application](../../../develop/build-apps/)
