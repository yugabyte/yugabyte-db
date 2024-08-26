---
title: Connect to clusters in YugabyteDB Aeon
linkTitle: Connect to clusters
description: Connect to clusters in YugabyteDB Aeon.
headcontent: Connect using Cloud Shell, a client shell, and from applications
image: /images/section_icons/index/quick_start.png
aliases:
  - /preview/deploy/yugabyte-cloud/connect-to-clusters/
  - /preview/yugabyte-cloud/connect-to-clusters/
  - /preview/yugabyte-cloud/cloud-basics/connect-to-clusters/
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-connect
    weight: 40
type: indexpage
---

Connect to clusters in YugabyteDB Aeon in the following ways:

| From | How |
| :--- | :--- |
| [Browser](connect-cloud-shell/) | Use Cloud Shell to connect to your database using any modern browser.<br>No need to set up an IP allow list, all you need is your database password.<br>Includes a built-in YSQL quick start guide. |
| [Desktop](connect-client-shell/) | Install the ysqlsh or ycqlsh client shells to connect to your database from your desktop.<br>YugabyteDB Aeon also supports psql and [third-party tools](../../integrations/tools/) such as pgAdmin.<br>Requires your computer to be added to the cluster [IP allow list](../cloud-secure-clusters/add-connections/) and an [SSL connection](../cloud-secure-clusters/cloud-authentication/). |
| [Applications](connect-applications/) | Obtain the parameters needed to connect your application driver to your cluster database.<br>Requires the VPC or machine hosting the application to be added to the cluster IP allow list and an SSL connection. |

{{<index/block>}}

  {{<index/item
    title="Cloud Shell"
    body="Connect from your browser."
    href="connect-cloud-shell/"
    icon="/images/section_icons/explore/cloud_native.png">}}

  {{<index/item
    title="Client shell"
    body="Connect from your desktop using a client shell."
    href="connect-client-shell/"
    icon="/images/section_icons/index/develop.png">}}

  {{<index/item
    title="Applications"
    body="Connect applications to your YugabyteDB Aeon clusters."
    href="connect-applications/"
    icon="/images/section_icons/develop/real-world-apps.png">}}

{{</index/block>}}
