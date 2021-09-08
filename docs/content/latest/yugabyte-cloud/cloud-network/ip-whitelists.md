---
title: Manage IP allow lists
linkTitle: IP allow lists
description: Manage IP allow lists for your cloud.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: ip-whitelists
    parent: cloud-network
    weight: 300
isTocNested: true
showAsideToc: true
---

To secure your cluster from distributed denial-of-service (DDoS) and brute force password attacks, you can restrict access to your clusters to IP addresses that you specify in IP allow lists. An IP allow list is simply a set of IP addresses and ranges that, when assigned to a cluster, grant access to connections made from those addresses; all other connections are ignored. Yugabyte Cloud only allows client connections to clusters from addresses in IP allow lists that have been assigned to the cluster.

The **IP Allow List** tab displays a list of IP allow lists configured for your cloud.

![Cloud Network IP Allow List page](/images/yb-cloud/cloud-networking-ip.png)

To view an existing IP allow list, select it in the list.

## Create an IP allow list

To create an IP allow list:

1. On the **IP Allow List** tab, click **Add IP Address** to display the **Add IP Allow List** sheet.
1. Enter a name and description for the allow list.
1. Enter the IP addresses and ranges. Each entry can either be a single IP address, a CIDR-notated range of addresses, or multiple comma-delimited addresses.
1. Click **Detect and add my IP to this list** to add the IP address of the computer you are using to access Yugabyte Cloud.
1. Click **Add** when you are done.

The allow list takes up to 30 seconds to become active.

To assign an IP allow list to a cluster, refer to [Assign IP allow lists](../../cloud-basics/add-connections/).

<!--
## Edit IP allow lists

To edit an IP allow list:

1. On the **IP Allow List** tab, select an item and click the Edit icon to display the **Edit IP Allow List** sheet.
1. Update the name and IP addresses.
-->
