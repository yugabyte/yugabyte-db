---
title: Connect applications
linkTitle: Connect applications
description: Connect applications to YugabyteDB Managed clusters
headcontent: Get the database connection parameters for your application
image: /images/section_icons/deploy/enterprise.png
menu:
  preview_yugabyte-cloud:
    identifier: connect-applications
    parent: cloud-connect
    weight: 30
type: docs
---

Applications connect to and interact with YugabyteDB using API client libraries, also known as client drivers. Because the YugabyteDB YSQL API is PostgreSQL-compatible, and the YCQL API has roots in the Apache Cassandra CQL, YugabyteDB supports many third-party drivers. YugabyteDB also supports [smart drivers](../../../drivers-orms/smart-drivers/), which extend PostgreSQL drivers to enable client applications to connect to YugabyteDB clusters without the need for external load balancers.

To connect to a YugabyteDB Managed cluster, you need to add the [cluster connection parameters](#get-the-cluster-connection-parameters) to your application code. How you update the application depends on the driver you are using.

For examples of applications that connect to YugabyteDB Managed using common drivers, refer to [Build an application](../../../develop/build-apps/).

For more information on YugabyteDB-compatible drivers, refer to [Drivers and ORMs](../../../drivers-orms/).

## Prerequisites

Before you can connect an application to a YugabyteDB Managed cluster, you need to do the following:

- Configure network access
- Download the cluster certificate

### Network access

#### IP allow list

To enable inbound network access from your application environment to a cluster, you need to add the IP addresses to the cluster IP allow list.

If your cluster is deployed in a peered VPC, you need to add the IP addresses of the peered application VPC to the cluster IP allow list.

By default, clusters deployed in a VPC do not expose any publicly-accessible IP addresses. To add public IP addresses, enable **Public Access** on the cluster **Settings > Network Access** tab.

For more information, refer to [IP allow list](../../cloud-secure-clusters/add-connections).

#### VPC network

If your cluster is deployed in a VPC, deploy your application in a VPC that is [peered](../../cloud-basics/cloud-vpcs/cloud-add-peering/) or [linked](../../cloud-basics/cloud-vpcs/cloud-add-endpoint/) with your cluster's VPC. Peered application VPCs also need to be added to the cluster IP allow list.

Clusters deployed in VPCs don't expose public IP addresses unless you explicitly turn on [Public Access](../../../yugabyte-cloud/cloud-secure-clusters/add-connections/#enabling-public-access). If you are connecting from a public IP address (for example, for testing, development, or running sample applications), enable Public Access on the cluster **Settings > Network Access** tab. Then use the public address in your application connection string. (This configuration is not recommended for production.)

#### Using smart drivers

To take advantage of smart driver load balancing features when connecting to clusters in YugabyteDB Managed, applications using smart drivers _must_ be deployed in a VPC that has been peered with the cluster VPC. If not deployed in a peered VPC, the smart driver falls back to the upstream driver behavior. For more information on smart drivers and using smart drivers with YugabyteDB Managed, refer to [YugabyteDB smart drivers for YSQL](../../../drivers-orms/smart-drivers/).

### Cluster certificate

YugabyteDB Managed clusters have TLS/SSL (encryption in-transit) enabled. Your driver connection properties need to include SSL parameters, and you need to download the cluster certificate to a location accessible to your application.

For information on SSL in YugabyteDB Managed, refer to [Encryption in transit](../../cloud-secure-clusters/cloud-authentication/).

## Get the cluster connection parameters

To connect an application to your cluster, add the cluster connection parameters to your application.

To get the connection parameters for your cluster:

1. On the **Clusters** tab, select the cluster.
1. Click **Connect**.
1. Click **Connect to your Application**.
1. Click **Download CA Cert** and install the cluster certificate on the computer running the application.
1. Choose the API used by your application, **YSQL** or **YCQL**, to display the corresponding connection parameters.
1. If your cluster is deployed in a VPC, choose **Private Address** if your application is in a peered VPC. Choose **Private Service Endpoint** if your application is in a linked VPC. Otherwise, choose Public Address (only available if you have enabled [Public Access](../../../yugabyte-cloud/cloud-secure-clusters/add-connections/#enabling-public-access); not recommended for production).

### Connection parameters

{{< tabpane text=true >}}

  {{% tab header="YSQL" lang="YSQL" %}}

Select **Connection String** to display the string that YSQL applications can use to connect. Select **Parameters** to display the individual parameters.

Here's an example of a generated `ysqlsh` string:

```sh
postgresql://<DB USER>:<DB PASSWORD>@us-west1.fa1b1ca1-b1c1-11a1-111b-ca111b1c1a11.aws.ybdb.io:5433/yugabyte? \
ssl=true& \
sslmode=verify-full& \
sslrootcert=<ROOT_CERT_PATH>
```

To use the string in your application, replace the following:

- `<DB USER>` with your database username.
- `<DB PASSWORD>` with your database password.
- `yugabyte` with the database name, if you're connecting to a database other than the default (yugabyte).
- `<ROOT_CERT_PATH>` with the path to the root certificate on your computer.

For example:

```sh
postgresql://admin:qwerty@us-west1.fa1b1ca1-b1c1-11a1-111b-ca111b1c1a11.aws.ybdb.io:5433/yugabyte?ssl=true& \
sslmode=verify-full&sslrootcert=~/.postgresql/root.crt
```

The connection string includes parameters for TLS settings (`ssl`, `sslmode`, and `sslrootcert`). The generated `ysqlsh` connection string uses the `verify-full` SSL mode by default.

For information on using other SSL modes, refer to [SSL modes in YSQL](../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql).

If you're connecting to a Hasura Cloud project, which doesn't use the CA certificate, select **Optimize for Hasura Cloud** to modify the string. Before using the string to connect in a Hasura project, be sure to encode any special characters. For an example of connecting a Hasura Cloud project to YugabyteDB Managed, refer to [Connect Hasura Cloud to YugabyteDB Managed](../../cloud-examples/hasura-cloud/).

  {{% /tab %}}

  {{% tab header="YCQL" lang="YCQL" %}}

To connect a YCQL application, use the connection parameters in your application to connect to your cluster. The parameters are:

- **LocalDatacenter** - The name of the local data center for the cluster.
- **Host** - The cluster host name.
- **Port** - The port number of the YCQL client API on the YugabyteDB database (9042).

To connect your application, do the following:

- Download the CA certificate.
- Add the YCQL java driver to your dependencies.
- Initialize SSLContext using the downloaded root certificate.

For an example of building a Java application connected to YugabyteDB Managed using the Yugabyte Java Driver for YCQL v4.6, refer to [Connect a YCQL Java application](../../cloud-examples/connect-ycql-application/).

  {{% /tab %}}

{{< /tabpane >}}

## Learn more

- [Add database users](../../cloud-secure-clusters/add-users/)
- [Build an application](../../../develop/build-apps/)
