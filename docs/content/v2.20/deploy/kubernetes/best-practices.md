---
title: Best practices
linkTitle: Best practices
description: Best practices
menu:
  v2.20:
    identifier: best-practices
    parent: deploy-kubernetes
    weight: 626
type: docs
---

## Local versus remote SSDs

Kubernetes provides you with an option of employing remote disks using dynamic provisioning or local storage which has to be preprovisioned.

Local storage provides great performance, but the data is not replicated and can be lost if the pod is moved to a different node for maintenance operations, or if a node fails. Remote storage has slightly lower performance but the data is resilient to failures.

The following table summarizes the tradeoffs of local vs remote storage:

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
    <td>Disk storage resilient to failures or pod movement</td>
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

While local storage offers higher throughput and lower latency at a lower cost, it comes with significant limitations. Maintenance operations like cluster node pool upgrades, or rolling operations to upgrade software or set flags can cause a pod to lose its local copy and require a full remote bootstrap of all local tablets from its peers. This can be time consuming for large databases. Further, for large clusters, there is always a non-zero risk of another pod movement happening during these maintenance operations, which would cause data loss for a subset of tablets. Therefore, using local storage is not recommended for most production use cases.


