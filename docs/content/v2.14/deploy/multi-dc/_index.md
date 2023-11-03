---
title: Multi-DC deployments
headerTitle: Multi-DC deployment
linkTitle: Multi-DC deployments
description: Deploy YugabyteDB across multiple data centers or cloud regions
headcontent: Deploy YugabyteDB across multiple data centers (DC)
image: /images/section_icons/explore/planet_scale.png
menu:
  v2.14:
    identifier: multi-dc
    parent: deploy
    weight: 631
type: indexpage
---
YugabyteDB is a geo-distributed SQL database that can be easily deployed across multiple data centers (DCs) or cloud regions. There are two primary configurations for such multi-DC deployments.

The first configuration uses a single cluster stretched across 3 or more data centers with data getting automatically sharded across all data centers. This configuration is default for [Spanner-inspired databases](../../architecture/docdb/) like YugabyteDB. Data replication across data centers is synchronous and is based on the Raft consensus protocol. This means writes are globally consistent and reads are either globally consistent or timeline consistent (when application clients use follower reads). Additionally, resilience against data center failures is fully automatic. This configuration has the potential to incur Wide Area Network (WAN) latency in the write path if the data centers are geographically located far apart from each other and are connected through the shared/unreliable Internet.

For users not requiring global consistency and automatic resilience to data center failures, the WAN latency can be eliminated altogether through the second configuration where two independent, single-DC clusters are connected through xCluster replication based on [Change Data Capture](../../architecture/docdb-replication/change-data-capture/).

[9 Techniques to Build Cloud-Native, Geo-Distributed SQL Apps with Low Latency](https://www.yugabyte.com/blog/9-techniques-to-build-cloud-native-geo-distributed-sql-apps-with-low-latency/) highlights the various multi-DC deployment strategies for a distributed SQL database like YugabyteDB. Note that YugabyteDB is the only Spanner-inspired distributed SQL database to support a 2DC deployment.

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="3dc-deployment/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png"  aria-hidden="true" />
        <div class="title">Three or more data centers (3DC)</div>
      </div>
      <div class="body">
        Deploy a single cluster across three or more data centers with global consistency
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="../kubernetes/multi-cluster/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png"  aria-hidden="true" />
        <div class="title">Three or more data centers (3DC) on K8S</div>
      </div>
      <div class="body">
        Deploy a single cluster across three or more Kubernetes clusters, each deployed in a different data center or region
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="async-replication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png"  aria-hidden="true" />
        <div class="title">xCluster replication </div>
      </div>
      <div class="body">
        Enable unidirectional and bidirectional replication
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="read-replica-clusters/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png" aria-hidden="true" />
        <div class="title">Read replica clusters</div>
      </div>
      <div class="body">
        Deploy a read replica cluster to serve low-latency reads from a remote data center
      </div>
    </a>
  </div>
</div>
