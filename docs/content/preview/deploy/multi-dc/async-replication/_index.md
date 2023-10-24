---
title: xCluster deployments
headerTitle: xCluster deployment
linkTitle: xCluster deployments
description: Deploy unidirectional (master-follower) or bidirectional (multi-master) replication between universes
headContent: Unidirectional (master-follower) and bidirectional (multi-master) replication
image: /images/section_icons/explore/planet_scale.png
menu:
  preview:
    identifier: async-replication
    parent: multi-dc
    weight: 610
type: indexpage
---
By default, YugabyteDB provides synchronous replication and strong consistency across geo-distributed data centers. However, many use cases do not require synchronous replication or justify the additional complexity and operating costs associated with managing three or more data centers. A cross-cluster (xCluster) deployment provides asynchronous replication across two data centers or cloud regions. Using an xCluster deployment, you can use unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between two universes (aka data centers).

For information on xCluster deployment architecture and replication scenarios, see [xCluster replication](../../../architecture/docdb-replication/async-replication/).

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="async-deployment/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png"  aria-hidden="true" />
        <div class="title">Deploy xCluster</div>
      </div>
      <div class="body">
        Set up unidirectional or bidirectional replication.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="async-replication-transactional">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png"  aria-hidden="true" />
        <div class="title">Deploy transactional xCluster</div>
      </div>
      <div class="body">
        Set up transactional unidirectional or bidirectional replication.
      </div>
    </a>
  </div>

</div>
