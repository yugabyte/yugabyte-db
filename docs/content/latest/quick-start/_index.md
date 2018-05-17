---
title: Quick Start
linkTitle: Quick Start
description: Quick Start
image: /images/section_icons/index/quick_start.png
headcontent: The easiest way to get started with YugaByte DB is to create a multi-node local cluster. 
type: page
aliases:
  - /quick-start/
menu:
  latest:
    identifier: quick-start
    weight: 100
---

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="install/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/install.png" aria-hidden="true" />
        <div class="title">1. Install YugaByte DB</div>
      </div>
      <div class="body">
        Install the binary on macOS/Linux or use Docker/Kubernetes to run on any OS of choice.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="create-local-cluster/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">2. Create Local Cluster</div>
      </div>
      <div class="body">
        Create a 3-node local cluster.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="test-cassandra/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/test_cql.png" aria-hidden="true" />    
        <div class="title">3. Test YCQL API</div>
      </div>
      <div class="body">
        Test Cassandra-compatible Yugabyte Cloud Query Language (YCQL) API.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="test-redis/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/test_redis.png" aria-hidden="true" />
        <div class="title">4. Test YEDIS API</div>
      </div>
      <div class="body">
        Test Redis-compatible YugabytE DIctionary Service (YEDIS) API.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="test-postgresql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/api-icon.png" aria-hidden="true" />
        <div class="title">5. Test PostgreSQL API</div>
      </div>
      <div class="body">
        Test PostgreSQL API.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="run-sample-apps/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/sample_apps.png" aria-hidden="true" />  
        <div class="title">6. Run Sample Apps</div>
      </div>
      <div class="body">
        Run pre-packaged sample apps.
      </div>
    </a>
  </div>
</div>

{{< note title="Note" >}}
Running local clusters is not recommended for production environments. You can either deploy the [Community Edition] (../deploy/multi-node-cluster/) manually on a set of instances or use the [Enterprise Edition](../deploy/enterprise-edition/) that automates all day-to-day operations including cluster administration across all major public clouds as well as on-premises datacenters.
{{< /note >}}
