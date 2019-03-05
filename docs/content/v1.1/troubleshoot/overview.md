---
title: Troubleshooting Overview
linkTitle: Troubleshooting Overview
description: Troubleshooting Overview
aliases:
  - /troubleshoot/overview/
menu:
  v1.1:
    identifier: troubleshooting-overview
    parent: troubleshoot
    weight: 702
isTocNested: true
showAsideToc: true
---

## 1. Check that YugaByte DB is running

First, ensure that the expected YugaByte DB processes are running on the current node.
At a minimum, the tserver process needs to be running to be able to connect to this node with a CQL client or application.

Additionally, depending on the setup, you might expect a master process to also be running on this node.
Follow the instructions on the [check processes](../nodes/check-processes/) page.

## 2. Check cluster level issues

Next, check the list of [cluster issues](../cluster) and the respective fixes for each of them.

## 3. Check logs

Inspect the YugaByte logs for more details on your issue. See [this](../nodes/check-logs) page for more details on where to find and how to understand the YugaByte log files.

## 4. File an issue

If you could not find a solution to your problem in these docs, please file a [GitHub issue](https://github.com/YugaByte/yugabyte-db/issues) describing your specific problem.

