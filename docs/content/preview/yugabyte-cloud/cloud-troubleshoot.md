---
title: Troubleshoot YugabyteDB Aeon
headerTitle: Troubleshoot
linkTitle: Troubleshoot
description: Troubleshoot issues in YugabyteDB Aeon.
headcontent: Diagnose and troubleshoot issues with YugabyteDB clusters and YugabyteDB Aeon
menu:
  preview_yugabyte-cloud:
    identifier: cloud-troubleshoot
    parent: yugabytedb-managed
    weight: 850
type: docs
---

If you are unable to reach YugabyteDB Aeon or having issues, first check the [status](https://status.yugabyte.cloud/).

## Connectivity

### Connection timed out

If you are connecting to a cluster and the cluster does not respond, and the connection eventually times out with the following error:

```output
ysqlsh: could not connect to server: Operation timed out
    Is the server running on host "4477b44e-4f4c-4ee4-4f44-f44e4abf4f44.aws.ybdb.io" (44.144.244.144) and accepting
    TCP/IP connections on port 5433?
```

- If you are trying to connect to a cluster from your local computer, add your computer to the cluster [IP allow list](../cloud-secure-clusters/add-connections/). If your IP address has changed, add the new IP address.

- If your cluster is deployed in a VPC and you are trying to connect from a public address (that is, outside your VPC network), you need to enable **Public Access** on the **Settings > Network Access** tab and connect to the cluster public IP address that is exposed.

- If your cluster is deployed in a VPC and you are trying to connect from a peered VPC, add one or more IP addresses from the peered VPC to the cluster IP allow list.

- If you are using a secure internet/web gateway, whitelist `*.ybdb.io` to allow access to clusters.

### Connection closed in Cloud Shell

If you are connected to a cluster in Cloud Shell and the message Connection Closed appears.

Cloud Shell has a hard limit of 1 hour for connections. In addition, if a Cloud Shell session is inactive for more than five minutes (for example, if you switch to another browser tab), your browser may disconnect the session. Close the shell window and [launch a new session](../cloud-connect/connect-cloud-shell/).

### SSL off

If you are connecting to a cluster using YSQL and see the following error:

```output
ysqlsh: FATAL:  no pg_hba.conf entry for host "144.244.44.44", user "admin", database "yugabyte", SSL off
```

YugabyteDB Aeon clusters require an SSL connection. If you set `sslmode` to `disable`, your connection will fail. Refer to [SSL modes in YSQL](../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql).

If you are connecting to a cluster using YCQL and see the following error:

```output
Connection error: ('Unable to connect to any servers', {'44.144.44.4': ConnectionShutdown('Connection to 44.144.44.4 was closed',)})
```

Ensure you are using the `--ssl` option and the path to the cluster CA certificate is correct. YCQL connections require the `--ssl` option and the use of the certificate.

For information on connecting to clusters using a client shell, refer to [Connect via client shells](../cloud-connect/connect-client-shell/).

### Remaining connection slots are reserved

If your application returns the error:

```output
org.postgresql.util.PSQLException: FATAL: remaining connection slots are reserved for non-replication superuser connections
```

Your application has reached the limit of available connections for the cluster:

- Sandbox clusters support up to 15 simultaneous connections.
- Dedicated clusters support 15 simultaneous connections per vCPU. For example, a 3-node cluster with 4 vCPUs per node can support 15 x 3 x 4 = 180 connections.

A solution would be to use a connection pooler. Depending on your use case, you may also want to consider scaling your cluster.

### Connection dropped during copy operation

If your application returns the error:

```output
ssl syscall error eof detected connection to server was lost
```

If you are using a Sandbox cluster and the [COPY command](../../api/ysql/the-sql-language/statements/cmd_copy/) (or using a tool that uses COPY), you may be exceeding the limited memory available in your Sandbox. The COPY command inserts data in a single transaction up to the [rows_per_transaction](../../api/ysql/the-sql-language/statements/cmd_copy/#rows-per-transaction) setting, which is 20k by default. The combination of a large number of columns and the number of rows being inserted in a single transaction may be too much load for a Sandbox cluster.

Try the following workarounds:

- Lower the value of `rows_per_transaction`. This will depend on the number of columns on the table, their types, and the length of those values. For example, columns with blob types or lengthy strings will be more likely to cause issues. Refer to [Import with skipping rows](../../api/ysql/the-sql-language/statements/cmd_copy/#import-with-skipping-rows).
- Open the import file and manually split the COPY command into multiple COPY commands.
- [Request a Free Trial](../managed-freetrial/).

### Application fails to connect

If the password for the YugabyteDB database account you are using to connect contains special characters (#, %, ^), the driver may fail to parse the URL.

Be sure to encode any special characters in your connection string.

### Password failure connecting to the database

Ensure that you have entered the correct password for the cluster database you are trying to access; refer to the cluster database admin credentials file you downloaded when you created the cluster. The file is named `<cluster name> credentials.txt`.

The database admin credentials are separate from your YugabyteDB Aeon credentials, which are used exclusively to log in to YugabyteDB Aeon.

If you are a database user who was added to the database by an administrator, ask your administrator to either re-send your credentials or [change your database password](../cloud-secure-clusters/add-users/).

Verify the case of the user name. Similarly to SQL and CQL, YSQL and YCQL are case-insensitive. When adding roles, names are automatically converted to lowercase. For example, the following command:

```sql
CREATE ROLE Alice LOGIN PASSWORD 'Password';
```

creates the user "alice". If you subsequently try to log in as "Alice", the login will fail. To use a case-sensitive name for a role, enclose the name in quotes. For example, to create the role "Alice", use `CREATE ROLE "Alice"`.

If you are the database admin and are unable to locate your database admin credentials file, contact {{% support-cloud %}}.

### VPC networking

If you have set up a VPC network and are unable to connect, verify the following.

#### VPC status is Failed

If you are unable to successfully create the VPC, contact {{% support-cloud %}}.

#### Peering connection status is Pending

A peering connection status of _Pending_ indicates that you need to configure your cloud provider to accept the connection. Refer to [Accept the peering request in AWS](../cloud-basics/cloud-vpcs/cloud-add-vpc-aws/#accept-the-peering-request-in-aws) or [Complete the peering in GCP](../cloud-basics/cloud-vpcs/cloud-add-vpc-gcp/#complete-the-peering-in-gcp).

#### Peering connection status is Expired (AWS only)

The peering request was not accepted. Recreate the peering connection.

#### Peering connection status is Failed

Select the peering request to display the **Peering Details** sheet and check the **Peered VPC Details** to ensure you entered the correct details for the cloud provider and application VPC.

#### VPC and peering connection are active but your application cannot connect to the cluster

Add the application VPC CIDR address to the [cluster IP allow list](../cloud-secure-clusters/add-connections/). Even with connectivity established between VPCs, the cluster cannot accept connections until the application VPC IP addresses are added to the IP allow list.

## Database management

### Permission denied, must be superuser

If you execute a YSQL command and receive the following error:

```output
ERROR:  permission denied to [...]
HINT:  Must be superuser to [...].
```

For security reasons, the database admin user is not a superuser. The admin user is a member of yb_superuser, which does allow most operations. For more information on database roles and privileges in YugabyteDB Aeon, refer to [Database authorization in YugabyteDB Aeon clusters](../cloud-secure-clusters/cloud-users/). If you need to perform an operation that requires superuser privileges, contact {{% support-cloud %}}.

### I need to change my database admin password

YugabyteDB uses [role-based access control](../../secure/authorization/) (RBAC) to [manage database authorization](../cloud-secure-clusters/cloud-users/). To change your database admin password, you need to connect to the cluster and use the ALTER ROLE statement. Refer to [Add database users](../cloud-secure-clusters/add-users/).

## Cluster management

### You are editing your cluster infrastructure and are unable to reduce disk size per node

If you increased the disk size per node for your cluster, you cannot reduce it. If you need to reduce the disk size for your cluster, contact {{% support-cloud %}}.

### You edited your cluster infrastructure and status is stuck at 'Editing cluster'

If you changed the number of nodes in your cluster (horizontal scaling), the length of time that the operation takes depends on the quantity of data in your cluster, as adding or removing nodes requires moving data between nodes. For example, when you remove nodes, the data must be drained from the nodes to be removed to the other nodes in the cluster. This can take awhile (even hours) for large datasets.

On the cluster **Nodes** tab, check the Memory Used column of the nodes to be removed. You should be able to see the nodes slowly draining as the data migrates.

If the condition persists, contact {{% support-cloud %}}.
