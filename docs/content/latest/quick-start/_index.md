---
title: Quick Start
description: Quick Start
type: page
aliases:
  - /quick-start/
menu:
  latest:
    identifier: quick-start
    weight: 100
---

The easiest way to get started with the YugaByte DB is to create a multi-node local cluster on your laptop or desktop. 

<div>
  <a class="section-link icon-offset" href="install/">
    <div class="icon">
      <img src="/images/section_icons/quick_start/install.png" aria-hidden="true" />
    </div>
    <div class="text">
      1. Install YugaByte DB
      <div class="caption">Install the binary on Linux/macOS or use Docker/Kubernetes to run on any OS of choice.</div>
    </div>
  </a>

  <a class="section-link icon-offset" href="create-local-cluster/">
    <div class="icon">
      <img src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
    </div>
    <div class="text">
      2. Create Local Cluster
      <div class="caption">Create a 3 node local cluster.</div>
    </div>
  </a>

  <a class="section-link icon-offset" href="test-cassandra/">
    <div class="icon">
      <img src="/images/section_icons/quick_start/test_cql.png" aria-hidden="true" />
    </div>
    <div class="text">
      3. Test Cassandra API
      <div class="caption">Test YugaByte DB Cassandra API.</div>
    </div>
  </a>

  <a class="section-link icon-offset" href="test-redis/">
    <div class="icon">
      <img src="/images/section_icons/quick_start/test_redis.png" aria-hidden="true" />
    </div>
    <div class="text">
      4. Test Redis API
      <div class="caption">Test YugaByte DB Redis API.</div>
    </div>
  </a>

  <a class="section-link icon-offset" href="test-postgresql/">
    <div class="icon">
      <img src="/images/section_icons/quick_start/test_redis.png" aria-hidden="true" />
    </div>
    <div class="text">
      5. Test PostgreSQL API (beta)
      <div class="caption">Test YugaByte DB PostgreSQL API (in beta).</div>
    </div>
  </a>

  <a class="section-link icon-offset" href="run-sample-apps/">
    <div class="icon">
      <img src="/images/section_icons/quick_start/sample_apps.png" aria-hidden="true" />
    </div>
    <div class="text">
      6. Run Sample Apps
      <div class="caption">Run pre-packaged sample apps.</div>
    </div>
  </a>
</div>

{{< note title="Note" >}}
Running local clusters is not recommended for production environments. You can either deploy the [Community Edition] (../deploy/multi-node-cluster/) manually on a set of instances or use the [Enterprise Edition](../deploy/enterprise-edition/) that automates all day-to-day operations including cluster administration across all major public clouds as well as on-premises datacenters.
{{< /note >}}
