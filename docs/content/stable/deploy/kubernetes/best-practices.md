---
title: Best practices
linkTitle: Best practices
description: Best practices
menu:
  stable:
    identifier: best-practices
    parent: deploy-kubernetes
    weight: 626
type: docs
---

## Local versus remote SSDs

Kubernetes provides you with an option of employing remote disks using dynamic provisioning or local storage which has to be preprovisioned.

Local storage provides great performance, but the data is not replicated and can be lost if the node fails. This option is ideal for databases, such as YugabyteDB, that manage their own replication and can guarantee high availability (HA).

Remote storage has slightly lower performance but the data is resilient to failures. This type of storage is absolutely essential for databases that do not offer HA (for example, traditional relational databases, such as PostgreSQL and MySQL).

The following table summarizes the features and when to use local or remote storage:

<table>
  <tr>
    <th>Feature</th>
    <th>Local SSD storage</th>
    <th>Remote SSD storage</th>
  </tr>
  <tr>
    <td>Provision large disk capacity per node</td>
    <td>Depends on cloud-provider</td>
    <td>Yes</td>
  </tr>
  <tr>
    <td>Ideal deployment strategy with YugabyteDB</td>
    <td>Use for latency sensitive apps <br> Add remote storage to increase capacity / cost-efficient tiering</td>
    <td>Use for large disk capacity per node</td>
  </tr>
  <tr>
    <td>Disk storage resilient to failures</td>
    <td>No</td>
    <td>Yes</td>
  </tr>
  <tr>
    <td>Performance - latency</td>
    <td>Lower</td>
    <td>Higher</td>
  </tr>
  <tr>
    <td>Performance - throughput</td>
    <td>Higher</td>
    <td>Lower</td>
  </tr>
  <tr>
    <td>Typical cost characteristics</td>
    <td>Lower</td>
    <td>Higher</td>
  </tr>
  <tr>
    <td>Kubernetes provisioning scheme</td>
    <td>Pre-provisioned</td>
    <td>Dynamic provisioning</td>
  </tr>
</table>


It is generally preferable to use local storage where possible for higher performance and lower costs. For information on how to deploy YugabyteDB on Kubernetes using local SSDs, see [Single-zone GKE](/preview/deploy/kubernetes/single-zone/gke/statefulset-yaml-local-ssd/).

