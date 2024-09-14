---
title: Cluster-level issues
linkTitle: Cluster-level issues
description: Cluster-level issues
image: /images/section_icons/quick_start/create_cluster.png
headcontent: Diagnostics and solutions for typical YugabyteDB cluster issues.
aliases:
  - /troubleshoot/cluster/
menu:
  preview:
    identifier: troubleshoot-cluster
    parent: troubleshoot
    weight: 100
type: indexpage
---

{{<index/block>}}

  {{<index/item
    title="YCQL API connection problems"
    body="Troubleshoot issues related to connecting to the Cassandra-compatible YCQL API service."
    href="connect-ycql/"
    icon="/images/section_icons/troubleshoot/troubleshoot.png">}}

  {{<index/item
    title="Recover YB-TServer, YB-Master, and node"
    body="Recover failed nodes to get the cluster back on optimal settings."
    href="recover_server/"
    icon="/images/section_icons/troubleshoot/troubleshoot.png">}}

  {{<index/item
    title="Replace a failed YB-TServer"
    body="Replace a failed YB-TServer in a YugabyteDB cluster."
    href="replace_tserver/"
    icon="/images/section_icons/troubleshoot/troubleshoot.png">}}

  {{<index/item
    title="Replace a failed YB-Master"
    body="Replace a failed YB-Master in a YugabyteDB cluster."
    href="replace_master/"
    icon="/images/section_icons/troubleshoot/troubleshoot.png">}}

  {{<index/item
    title="Replace failed peers"
    body="Perform manual remote bootstrap of failed peers."
    href="replace_failed_peers/"
    icon="/images/section_icons/troubleshoot/troubleshoot.png">}}

   {{<index/item
    title="Recover YB-TServer from crash loop"
    body="Find faulty tablets and their data on disk, and then remove it."
    href="failed_tablets/"
    icon="/images/section_icons/troubleshoot/troubleshoot.png">}}

   {{<index/item
    title="Troubleshoot performance issues"
    body="Investigate and troubleshoot performance of your YugabyteDB clusters."
    href="performance-troubleshooting/"
    icon="/images/section_icons/troubleshoot/troubleshoot.png">}}

{{</index/block>}}
