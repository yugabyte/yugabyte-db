---
title: Troubleshooting Overview
weight: 810
---

## Check that YugaByte is running

First, ensure that the expected YugaByte DB processes are running on the current node.
At a minimum, the tserver process needs to be running to be able to connect to this node with a CQL client or application.
Additionally, depending on the setup, you might expect a master process to also be running on this node.
Follow the instructions on the [check processes](/troubleshoot/nodes/check-processes/) page.

## Check common issues

Next, check the list of [common issues](/troubleshoot/common-issues) and the respective fixes for each of them.

## Check YugaByte logs

Inspect the yugabyte logs for more details on your issue. See [this](/troubleshoot/nodes/check-logs) page for more details on where to find and how to understand the YugaByte log files.

## File an issue

If you could not find a solution to your problem in these docs, please reach out to us on our [forum](https://forum.yugabyte.com/) or file a [GitHub issue](https://github.com/YugaByte/yugabyte-db/issues) describing your specific problem.

