---
title: Troubleshoot Yugabyte Cloud
linkTitle: Troubleshoot
description: Troubleshoot issues in Yugabyte Cloud.
headcontent:
image: /images/section_icons/index/quick_start.png
menu:
  latest:
    identifier: cloud-troubleshoot
    parent: yugabyte-cloud
    weight: 850
isTocNested: true
showAsideToc: true
---

## Connectivity

### Connection timed out

If you are connecting to a cluster and the cluster does not respond, and the connection eventually times out with the following error:

```output
ysqlsh: could not connect to server: Operation timed out
    Is the server running on host "4477b44e-4f4c-4ee4-4f44-f44e4abf4f44.aws.ybdb.io" (44.144.244.144) and accepting
    TCP/IP connections on port 5433?
```

If you are trying to connect to a cluster from your local computer, add your computer to the cluster [IP allow list](../cloud-secure-clusters/add-connections/). If your IP address has changed, add the new IP address.

If you have a VPC configured, add one or more IP addresses from the peered VPC to the cluster [IP allow list](../cloud-secure-clusters/add-connections/).

### SSL off

If you are connecting to a cluster and see the following error:

```output
ysqlsh: FATAL:  no pg_hba.conf entry for host "144.244.44.44", user "admin", database "yugabyte", SSL off
```

Yugabyte Cloud clusters require an SSL connection. If you set `sslmode` to `disable`, your connection will fail. Refer to [SSL modes in YSQL](../cloud-connect/connect-client-shell/#ssl-modes-in-ysql).

### Application fails to connect

If the password for the YugabyteDB database account you are using to connect contains special characters (#, %, ^), the driver may fail to parse the URL.

Be sure to encode any special characters in your connection string.

### VPC networking

If you have set up a VPC network and are unable to connect, verify the following.

#### VPC status is Failed

If you are unable to successfully create the VPC, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

#### Peering connection status is Pending

A peering connection status of _Pending_ indicates that you need to configure your cloud provider to accept the connection. Refer to [Create a peering connection](../cloud-secure-clusters/cloud-vpcs/cloud-add-peering).

#### Peering connection status is Expired (AWS only)

The peering request was not accepted. Recreate the peering connection.

#### Peering connection status is Failed

Select the peering request to display the **Peering Details** sheet and check the **Peered VPC Details** to ensure you entered the correct details for the cloud provider and application VPC.

#### VPC and peering connection are active but your application cannot connect to the cluster

Add the application VPC CIDR address to the [cluster IP allow list](../cloud-secure-clusters/add-connections/). Even with connectivity established between VPCs, the cluster cannot accept connections until the application VPC IP addresses are added to the IP allow list.
