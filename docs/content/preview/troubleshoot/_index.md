---
title: Troubleshoot YugabyteDB
headerTitle: Troubleshoot YugabyteDB
linkTitle: Troubleshoot
description: Diagnose and solve YugabyteDB cluster and node issues
image: /images/section_icons/index/troubleshoot.png
menu:
  preview:
    identifier: troubleshoot
    parent: launch-and-manage
type: indexpage
cascade:
  unversioned: true
---

Typically, troubleshooting involves a number of steps that are rather consistent regardless of the particular situation. These steps include the following:

1. Verify that YugabyteDB is running: you need to ensure that the expected YugabyteDB processes are running on the current node. At a minimum, the YB-TServer process must be running to be able to connect to the node with a client or application.

    Additionally, depending on the setup, you might expect a YB-Master process to run on this node.

    For more information, see [Check servers](nodes/check-processes/).

2. Check [cluster-level issues](cluster/) and their solutions.

3. Check logs: you should inspect the YugabyteDB logs for additional details on your issue. For more information, see [Inspect logs](nodes/check-logs/).

4. Explore knowledge base articles: you can find additional troubleshooting resources and information on Yugabyte [Support page](https://support.yugabyte.com/).

5. File an issue: if you can't find a solution to your problem, file a [GitHub issue](https://github.com/yugabyte/yugabyte-db/issues) describing your specific problem.

{{<index/block>}}

  {{<index/item
    title="Cluster-level issues and performance"
    body="Troubleshoot common YugabyteDB cluster issues and improve cluster performance."
    href="cluster/"
    icon="/images/section_icons/quick_start/create_cluster.png">}}

  {{<index/item
    title="Node-level issues"
    body="Diagnose and solve issues on an individual YugabyteDB node."
    href="nodes/"
    icon="/images/section_icons/architecture/concepts/universe.png">}}

   {{<index/item
    title="YSQL issues"
    body="Solve issues related to YSQL."
    href="ysql-issues/"
    icon="/images/section_icons/api/ysql.png">}}

   {{<index/item
    title="Other issues"
    body="Understand how to address other issues related to YugabyteDB."
    href="other-issues/"
    icon="fa-thin fa-circle-exclamation">}}

{{</index/block>}}
