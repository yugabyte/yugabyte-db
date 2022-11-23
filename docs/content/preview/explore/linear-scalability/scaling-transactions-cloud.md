---
title: Scaling transactions
headerTitle: Scaling concurrent transactions
linkTitle: Scaling concurrent transactions
description: Scaling concurrent transactions in YugabyteDB Managed.
headcontent: Horizontal scale-out and scale-in in YugabyteDB
menu:
  preview:
    name: Scaling transactions
    identifier: explore-transactions-scaling-transactions-3-ysql
    parent: explore-scalability
    weight: 200
type: docs
---

With YugabyteDB, you can add nodes to upscale your cluster efficiently and reliably to achieve more read and write IOPS (input/output operations per second), without any downtime.

This tutorial shows how YugabyteDB can scale seamlessly while running a read-write workload. Using the [YB Simulation Base Demo application](https://github.com/yugabyte/yb-simulation-base-demo-app) against a three-node cluster with a replication factor of 3, you add a node while the workload is running. Using the built-in metrics, you can observe how the cluster scales out by verifying that the number of read and write IOPS are evenly distributed across all the nodes at all times.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../scaling-transactions-cloud/" class="nav-link active">
              <img src="/icons/cloud-icon.svg" alt="Icon">
Use a cloud cluster
    </a>
  </li>
  <li>
    <a href="../scaling-transactions/" class="nav-link">
              <img src="/icons/server-iconsvg.svg" alt="Icon">
Use a local cluster
    </a>
  </li>
</ul>

{{% explore-setup-multi-cloud %}}

Follow the setup instructions to start a three-node cluster, connect the YB Simulation Base Demo application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.
