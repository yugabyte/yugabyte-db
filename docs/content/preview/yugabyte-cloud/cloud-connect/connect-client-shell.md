---
title: Connect via client shells
linkTitle: Client shell
description: Connect to YugabyteDB Aeon clusters from your desktop using a client shell
headcontent: Connect to YugabyteDB Aeon databases from your desktop
menu:
  preview_yugabyte-cloud:
    identifier: connect-client-shell
    parent: cloud-connect
    weight: 20
type: docs
---

Connect to your YugabyteDB cluster database from your desktop using the YugabyteDB [ysqlsh](../../../admin/ysqlsh/) and [ycqlsh](../../../admin/ycqlsh) client shells installed on your computer. Because YugabyteDB is compatible with PostgreSQL and Cassandra, you can also use [psql](https://www.postgresql.org/docs/current/app-psql.html) and third-party tools to connect.

When connecting via a Yugabyte client shell, ensure you are running the latest versions of the shells (Yugabyte Client 2.6 or later). See [How do I connect to my cluster?](../../../faq/yugabytedb-managed-faq/#how-do-i-connect-to-my-cluster) in the FAQ for details.

## Prerequisites

Before you can connect a desktop client to a YugabyteDB Aeon cluster, you need to do the following:

- Configure network access
- Download the cluster certificate

### Network access

Before you can connect using a shell or other client, you need to add your computer to the cluster IP allow list.

By default, clusters deployed in a VPC do not expose any publicly-accessible IP addresses. To add public IP addresses, enable [Public Access](../../cloud-secure-clusters/add-connections/#enabling-public-access) on the cluster **Settings > Network Access** tab. Alternatively, use the [Cloud shell](../connect-cloud-shell/) instead.

For more information, refer to [IP allow list](../../cloud-secure-clusters/add-connections).

### Cluster certificate

YugabyteDB Aeon clusters have TLS/SSL (encryption in-transit) enabled. You need to download the cluster certificate to your computer.

For information on SSL in YugabyteDB Aeon, refer to [Encryption in transit](../../cloud-secure-clusters/cloud-authentication/).

## Connect using a client shell

Use the ysqlsh and ycqlsh shells to connect to and interact with YuagbyteDB using the YSQL and YCQL APIs respectively. You can download and install the YugabyteDB client shells and connect to your database using the following steps for either YSQL or YCQL.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#ysqlsh" class="nav-link active" id="ysqlsh-tab" data-toggle="tab" role="tab" aria-controls="ysqlsh" aria-selected="true">
      <i class="icon-postgres" aria-hidden="true"></i>
      ysqlsh
    </a>
  </li>
  <li>
    <a href="#ycqlsh" class="nav-link" id="ycqlsh-tab" data-toggle="tab" role="tab" aria-controls="ycqlsh" aria-selected="false">
      <i class="icon-cassandra" aria-hidden="true"></i>
      ycqlsh
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="ysqlsh" class="tab-pane fade show active" role="tabpanel" aria-labelledby="ysqlsh-tab">
  {{% includeMarkdown "connect/ysql.md" %}}
  </div>
  <div id="ycqlsh" class="tab-pane fade" role="tabpanel" aria-labelledby="ycqlsh-tab">
  {{% includeMarkdown "connect/ycql.md" %}}
  </div>
</div>

## Connect using psql

To connect using [psql](https://www.postgresql.org/docs/current/app-psql.html), first download the CA certificate for your cluster by clicking **Connect**, selecting **YugabyteDB Client Shell**, and clicking **Download CA Cert**. Then use the following connection string:

```sh
psql --host=<HOST_ADDRESS> --port=5433 \
--username=<DB USER> \
--dbname=yugabyte \
--set=sslmode=verify-full \
--set=sslrootcert=<ROOT_CERT_PATH>
```

Replace the following:

- `<HOST_ADDRESS>` with the value for host as shown under **Connection Parameters** on the **Settings > Infrastructure** tab for your cluster.
- `<DB USER>` with your database username.
- `yugabyte` with the database name, if you're connecting to a database other than the default (yugabyte).
- `<ROOT_CERT_PATH>` with the path to the root certificate on your computer.

For information on using other SSL modes, refer to [SSL modes in YSQL](../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql).

## Connect using third party clients

Because YugabyteDB is compatible with PostgreSQL and Cassandra, you can use third-party clients to connect to your YugabyteDB clusters in YugabyteDB Aeon.

To connect, follow the client's configuration steps for PostgreSQL or Cassandra, and use the following values:

- **host** as shown under **Connection Parameters** on the **Settings > Infrastructure** tab for your cluster.
- **port** 5433 for YSQL, 9042 for YCQL.
- **database** name; the default YSQL database is yugabyte.
- **username** and **password** of a user with permissions for the database; the default user is admin.

Your client may also require the use of the cluster's certificate. Refer to [Download the cluster certificate](../../cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate).

For detailed steps for configuring popular third party tools, see [Third party tools](../../../tools/).

## Related information

- [ysqlsh](../../../admin/ysqlsh/) — Overview of the command line interface (CLI), syntax, and commands.
- [YSQL API](../../../api/ysql/) — Reference for supported YSQL statements, data types, functions, and operators.
- [ycqlsh](../../../admin/ycqlsh/) — Overview of the command line interface (CLI), syntax, and commands.
- [YCQL API](../../../api/ycql/) — Reference for supported YCQL statements, data types, functions, and operators.

## Next steps

- [Add database users](../../cloud-secure-clusters/add-users/)
- [Connect an application](../connect-applications/)
