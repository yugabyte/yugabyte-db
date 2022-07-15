---
title: Troubleshooting
linkTitle: Troubleshooting
headerTitle: Troubleshooting overview
description: Troubleshooting overview
menu:
  preview:
    identifier: troubleshooting-overview
    parent: troubleshoot
    weight: 702
type: docs
---

Typically, troubleshooting involves a number of steps that are rather consistent regardless of the particular situation. These steps include the following:

1. Verifying that YugabyteDB is running: you need to ensure that the expected YugabyteDB processes are running on the current node. At a minimum, the YB-TServer process must be running to be able to connect to the node with a YCQL client or application.

   Additionally, depending on the setup, you might expect a YB-Master process to run on this node.

   For more information, see [Check processes](../nodes/check-processes/).

2. Checking [cluster-level issues](../cluster/) and their solutions.

3. Checking logs: you should inspect the YugabyteDB logs for additional details on your issue. For more information, see [Inspect logs](../nodes/check-logs/).

4. Exploring knowledge base articles: you can find additional troubleshooting resources and information on Yugabyte [Support page](https://support.yugabyte.com/).

5. Filing an issue: if you could not find a solution to your problem, please file a [GitHub issue](https://github.com/yugabyte/yugabyte-db/issues) describing your specific problem.