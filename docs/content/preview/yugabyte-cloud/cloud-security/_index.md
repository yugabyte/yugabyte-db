---
title: Security architecture
headerTitle: Security architecture
linkTitle: Security architecture
description: Security architecture of Yugabyte Cloud.
image: /images/section_icons/index/secure.png
headcontent: Review Yugabyte Cloud's security architecture and shared responsibility model.
section: YUGABYTE CLOUD
menu:
  latest:
    identifier: cloud-security
weight: 800
---

Yugabyte Cloud is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on public cloud providers such as Google Cloud Platform (GCP) and Amazon Web Services (AWS).

Yugabyte Cloud uses a shared responsibility model, where security and compliance is a shared responsibility between public cloud providers, Yugabyte, and Yugabyte Cloud customers.

The Yugabyte Cloud architecture is secure by default, and uses the following features to protect clusters and communication between clients and databases:

- encryption in transit
- encryption at rest
- limited network exposure
- authentication
- role-based access control for authorization

For information on how to configure the security features of clusters in Yugabyte Cloud, refer to [Secure clusters in Yugabyte Cloud](../cloud-secure-clusters/).

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-security-features/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/checklist.png" aria-hidden="true" />
        <div class="title">Security architecture</div>
      </div>
      <div class="body">
        Learn about Yugabyte Cloud's security architecture.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="shared-responsibility/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/grant-permissions.png" aria-hidden="true" />
        <div class="title">Shared responsibility model</div>
      </div>
      <div class="body">
        The Yugabyte Cloud shared responsibility model for security.
      </div>
    </a>
  </div>

</div>
