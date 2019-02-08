---
title: Community Edition Quick Start
linkTitle: Quick Start
description: Community Edition Quick Start
image: /images/section_icons/index/quick_start.png

headcontent: The easiest way to test YugaByte DB's basic features and APIs is to create a local multi-node cluster on a single host.
type: page
aliases:
  - /quick-start/
menu:
  latest:
    identifier: quick-start
    weight: 100
---

{{< note title="Note" >}}
We do not recommend a local multi-node cluster setup on a single host for production deployments or performance benchmarking. For those, consider deploying a true multi-node on multi-host setup using the <a href="../../latest/deploy">manual or orchestrated deployment steps</a>.
{{< /note >}}


## Get Started Yourself

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
    <a class="section-link icon-offset" href="test-ycql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/test_cql.png" aria-hidden="true" />
        <div class="title">3. Test YCQL API</div>
      </div>
      <div class="body">
        Test Cassandra-compatible YugaByte Cloud Query Language (YCQL) API.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="test-yedis/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/test_redis.png" aria-hidden="true" />
        <div class="title">4. Test YEDIS API</div>
      </div>
      <div class="body">
        Test Redis-compatible YugaByte Dictionary Service (YEDIS) API.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="test-ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/api-icon.png" aria-hidden="true" />
        <div class="title">5. Test YSQL API</div>
      </div>
      <div class="body">
        Test PostgreSQL-compatible YugaByte SQL (YSQL) API.
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

## Watch the Demo

<div class="video-wrapper">
{{< vimeo 297325701 >}}
</div>
