---
title: Network access
headerTitle: Network access
linkTitle: Network access
description: Configure Yugabyte Cloud endpoints, VPC peers, and IP whitelists.
image: /images/section_icons/secure/tls-encryption/connect-to-cluster.png
headcontent: Set up network access to your Yugabyte Cloud and clusters.
aliases:
  - /latest/deploy/yugabyte-cloud/manage-access/
  - /latest/yugabyte-cloud/manage-access/
menu:
  latest:
    identifier: cloud-network
    parent: yugabyte-cloud
    weight: 400
---

Set up network security for your cluster using the **Network Access** page.

To prevent distributed denial-of-service (DDoS) and brute force password attacks, Yugabyte Cloud restricts access to your clusters to specific networks that you authorize. To authorize your application server’s network and your local machine’s network, you add their public IP addresses to IP allow lists. An IP allow list is simply a set of IP addresses and ranges that, when assigned to a cluster, grant access to connections made from those addresses.

You can also set up dedicated Virtual Private Cloud (VPC) peers for your cluster for network isolation and lower latency.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ip-whitelists/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">IP allow lists</div>
      </div>
      <div class="body">
        Whitelist IP addresses to control who can connect to your clusters.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="vpc-peers/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/create_cluster.png" aria-hidden="true" />
        <div class="title">VPC peering</div>
      </div>
      <div class="body">
        Add VPC peers to allow applications running on other cloud instances to communicate with your YugabyteDB clusters.
      </div>
    </a>
  </div>
<!--
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="endpoints/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_universe.png" aria-hidden="true" />
        <div class="title">Manage Endpoints</div>
      </div>
      <div class="body">
        Manage the endpoints for connecting to clusters.
      </div>
    </a>
  </div>
-->
</div>
