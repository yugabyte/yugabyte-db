---
title: Assign IP allow lists
linkTitle: Assign IP allow lists
description: Assign IP allow lists to a cluster.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: add-connections
    parent: cloud-basics
    weight: 40
isTocNested: true
showAsideToc: true
---

Yugabyte Cloud only allows client connections to clusters from entries in IP allow lists that have been explicitly assigned to the cluster. Each entry can either be a single IP address, a CIDR-notated range of addresses, or a comma-delimited list of addresses.

The IP allow lists assigned to a cluster are listed under **Network Access** on the cluster **Settings** tab.

To add IP allow lists to a cluster:

1. On the **Clusters** page, select the cluster, and select the **Settings** tab.
1. Under **Network Access**, click **Add List** to display the **Add IP Allow List** sheet.
    \
    The sheet lists all IP allow lists that have been created for your cloud.

1. Select the box for the IP allow lists you want to assign to the cluster.
1. If you do not have any IP allow lists or want to create a new one, click **Create New List and Add to Cluster** and do the following:
    - Enter a name and description for the list.
    - Enter one or more IP addresses or CIDR ranges; delimit entries using commas or new lines.
    - Click **Detect and add my IP to this list** to add your own IP address.
1. Click **Save** when done.

The allow list takes up to 30 seconds to become active

Any IP allow list that you create is also added to your cloud settings. For information on managing cloud networking, refer to [Configure networking](../../cloud-network/).

## Next steps

- [Connect to your cluster](../connect-to-clusters)
- [Create a database](../create-databases)
- [Add database users](../add-users/)
- [Connect an application](../connect-application)
