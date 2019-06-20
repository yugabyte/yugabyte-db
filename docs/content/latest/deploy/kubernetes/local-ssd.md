---
title: Local SSD
linkTitle: Local SSD
description: Local SSD
aliases:
  - /deploy/kubernetes/local-ssd/
menu:
  latest:
    identifier: local-ssd
    parent: deploy-kubernetes
    weight: 622
---



<h2>Local vs remote SSDs</h2>

Kubernetes gives users the option of using remote disks using dynamic provisioning or local storage which has to be pre-provisioned.

Local storage gives great performance but the data is not replicated and can be lost if the node fails. This option is ideal for databases like YugaByte DB that manage their own replication and can guarantee HA.

Remote storage has slightly lower performance but the data is resilient to failures. This type of storage is absolutely essential for databases that do not offer HA (for example, traditional RDBMSs like Postgres and MySQL).

Below is a table that summarizes the features and when to use local vs remote storage.

<table>
  <tr>
    <th></th>
    <th>Local SSD storage</th> 
    <th>Remote SSD storage</th>
  </tr>
  <tr>
    <td>Provision large disk capacity per node</td>
    <td>Depends on cloud-provider</td> 
    <td>Yes</td>
  </tr>
  <tr>
    <td>Ideal deployment strategy with YugaByte DB</td>
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

Thus, it is generally preferable to use local storage where possible for higher performance and lower costs. The following section explains how to deploy YugaByte DB on Kubernetes using local SSDs.


<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#gke" class="nav-link active" id="gke-tab" data-toggle="tab" role="tab" aria-controls="gke" aria-selected="true">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Google Kubernetes Engine (GKE)
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="gke" class="tab-pane fade show active" role="tabpanel" aria-labelledby="gke-tab">
    {{% includeMarkdown "local-ssd/gke.md" /%}}
  </div>
</div>